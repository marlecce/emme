/* router.c - Routing module for the web server (TLS version)
 *
 * This module examines the HTTP request (HttpRequest) and, based on the routes
 * defined in the configuration (ServerConfig), decides whether to:
 *   - Serve a static file (using sendfile for zero-copy).
 *   - Forward the request to a backend (reverse proxy).
 *
 * In TLS mode, data sent to the client uses SSL_write() and data is read
 * using SSL_read().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include "http2_response.h"
#include "router.h"
#include "server.h"

/* serve_static_tls()
 *
 * If the HTTP request's path starts with a static route, constructs the full file path,
 * opens the file, and sends it to the client using SSL_write() and sendfile() for zero-copy.
 * If the file is not found, a 404 response is sent.
 */
int serve_static_tls(HttpRequest *req, ServerConfig *config, SSL *ssl)
{
    if (!req || !req->path)
        return -1;

    for (int i = 0; i < config->route_count; i++)
    {
        if (strcmp(config->routes[i].technology, "static") == 0)
        {
            size_t prefix_len = strlen(config->routes[i].path);
            if (strlen(req->path) < prefix_len)
                continue;
            if (strncmp(req->path, config->routes[i].path, prefix_len) == 0)
            {
                char filepath[512];
                if ((size_t)snprintf(filepath, sizeof(filepath), "%s%s",
                                     config->routes[i].document_root, req->path + prefix_len) >= sizeof(filepath))
                {
                    fprintf(stderr, "serve_static_tls: Path too long\n");
                    return -1;
                }
                int fd = open(filepath, O_RDONLY);
                if (fd < 0)
                {
                    const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                    SSL_write(ssl, not_found, strlen(not_found));
                    return -1;
                }
                off_t filesize = lseek(fd, 0, SEEK_END);
                lseek(fd, 0, SEEK_SET);
                char header[256];
                snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", filesize);
                SSL_write(ssl, header, strlen(header));
                // Here, we use sendfile() on the plain file descriptor.
                // Since sendfile() writes directly from fd to socket,
                // we need to flush the TLS record first.
                char filebuf[BUFFER_SIZE];
                ssize_t bytes;
                while ((bytes = read(fd, filebuf, sizeof(filebuf))) > 0)
                {
                    SSL_write(ssl, filebuf, bytes);
                }
                close(fd);
                return 0;
            }
        }
    }
    return -1;
}

/* proxy_bidirectional_tls()
 *
 * Implements a bidirectional forwarding loop between a TLS client and a backend server.
 * Data from the client is read with SSL_read() and forwarded to the backend via write(),
 * while data from the backend is read with read() and forwarded to the client using SSL_write().
 */
int proxy_bidirectional_tls(SSL *ssl, int backend_fd)
{
    int done = 0;
    char buf[BUFFER_SIZE];
    while (!done)
    {
        /* Read from backend (plain) */
        int n = read(backend_fd, buf, sizeof(buf));
        if (n > 0)
        {
            int sent = 0;
            while (sent < n)
            {
                int s = SSL_write(ssl, buf + sent, n - sent);
                if (s <= 0)
                {
                    done = 1;
                    break;
                }
                sent += s;
            }
        }
        else if (n < 0)
        {
            done = 1;
        }
        /* Read from client (TLS) */
        n = SSL_read(ssl, buf, sizeof(buf));
        if (n > 0)
        {
            int sent = 0;
            while (sent < n)
            {
                int s = send(backend_fd, buf + sent, n - sent, 0);
                if (s <= 0)
                {
                    done = 1;
                    break;
                }
                sent += s;
            }
        }
        else if (n < 0)
        {
            done = 1;
        }
    }
    return 0;
}

/* proxy_request_tls()
 *
 * Forwards the entire HTTP request to a backend for reverse proxy functionality.
 * Then activates proxy_bidirectional_tls() to forward data bidirectionally.
 */
int proxy_request_tls(HttpRequest *req, char *raw_request, int req_len, ServerConfig *config, SSL *ssl)
{
    for (int i = 0; i < config->route_count; i++)
    {
        if (strcmp(config->routes[i].technology, "reverse_proxy") == 0)
        {
            size_t prefix_len = strlen(config->routes[i].path);
            if (strlen(req->path) < prefix_len)
                continue;
            if (strncmp(req->path, config->routes[i].path, prefix_len) == 0)
            {
                char ip[64];
                int port;
                if (sscanf(config->routes[i].backend, "%63[^:]:%d", ip, &port) != 2)
                    return -1;
                int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (backend_fd < 0)
                    return -1;
                struct sockaddr_in backend_addr;
                memset(&backend_addr, 0, sizeof(backend_addr));
                backend_addr.sin_family = AF_INET;
                backend_addr.sin_port = htons(port);
                if (inet_pton(AF_INET, ip, &backend_addr.sin_addr) <= 0)
                {
                    close(backend_fd);
                    return -1;
                }
                if (connect(backend_fd, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0)
                {
                    close(backend_fd);
                    return -1;
                }
                int sent = 0;
                while (sent < req_len)
                {
                    int n = send(backend_fd, raw_request + sent, req_len - sent, 0);
                    if (n <= 0)
                        break;
                    sent += n;
                }
                proxy_bidirectional_tls(ssl, backend_fd);
                close(backend_fd);
                return 0;
            }
        }
    }
    return -1;
}

/* route_request_tls()
 *
 * Decides how to handle the request:
 *   - If the request path is "/", serves a default HTML page.
 *   - Otherwise, it first tries to serve the request as a static file.
 *   - If that fails, it attempts to forward the request to the backend via reverse proxy.
 * Note: All communication with the client is via the SSL pointer.
 */
int route_request_tls(HttpRequest *req, const char *raw, size_t raw_len, ServerConfig *config, SSL *ssl, Http2Response *h2resp)
{
    if (h2resp)
    {
        snprintf(h2resp->body, sizeof(h2resp->body), "<html><body>Hello, HTTP/2!</body></html>");
        h2resp->body_len = strlen(h2resp->body);
        if (h2resp->body_len == 0) {
            h2resp->body[0] = '\n'; // or ' '
            h2resp->body[1] = '\0';
            h2resp->body_len = 1;
        }
        h2resp->status_code = 200;
        strcpy(h2resp->status_text, "OK");
        strcpy(h2resp->content_type, "text/html");
        h2resp->num_headers = 0;
        char status_str[4];
        snprintf(status_str, sizeof(status_str), "%d", h2resp->status_code);
        h2resp->headers[h2resp->num_headers++] = MAKE_NV(":status", status_str);
        h2resp->headers[h2resp->num_headers++] = MAKE_NV("content-type", h2resp->content_type);
        char clen[32];
        snprintf(clen, sizeof(clen), "%zu", h2resp->body_len);
        h2resp->headers[h2resp->num_headers++] = MAKE_NV("content-length", clen);
        // Add more headers as needed
        return 0;
    }

    if (strcmp(req->path, "/") == 0)
    {
        const char *html_content = "<html><head><title>High Performance Web Server</title></head>"
                                   "<body><h1>Welcome to High Performance Web Server</h1>"
                                   "<p>This server is designed to outperform Nginx and Apache by utilizing "
                                   "advanced I/O techniques, a modular architecture, and an efficient reverse proxy mechanism.</p>"
                                   "</body></html>";
        size_t content_length = strlen(html_content);
        char header[256];
        snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n", content_length);
        SSL_write(ssl, header, strlen(header));
        SSL_write(ssl, html_content, content_length);
        return 0;
    }

    if (serve_static_tls(req, config, ssl) == 0)
        return 0;
    if (proxy_request_tls(req, (char *)raw, raw_len, config, ssl) == 0)
        return 0;

    const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    SSL_write(ssl, not_found, strlen(not_found));
    return -1;
}
