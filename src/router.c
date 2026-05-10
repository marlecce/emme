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
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "router.h"
#include "log.h"
#include "config.h"
#include "tls.h"
#include "http2_response.h"
#include "metrics.h"

static int ssl_write_all(SSL *ssl, const char *buf, size_t len);

typedef enum {
    STATIC_LOOKUP_ERROR = -1,
    STATIC_LOOKUP_NO_ROUTE = 0,
    STATIC_LOOKUP_READY = 1,
    STATIC_LOOKUP_NOT_FOUND = 2,
    STATIC_LOOKUP_FORBIDDEN = 3,
} StaticLookupResult;

static int send_health_response(SSL *ssl, Http2Response *h2resp)
{
    const char *body = "{\"status\":\"ok\"}";
    size_t body_len = strlen(body);
    shutdown_state_t state = atomic_load(&g_shutdown_ctx.state);
    
    if (state == SHUTDOWN_STATE_DRAINING) {
        const char *draining_body = "{\"status\":\"draining\",\"reason\":\"graceful_shutdown\"}";
        size_t draining_len = strlen(draining_body);
        
        if (h2resp) {
            h2_response_init(h2resp);
            h2_response_set_status(h2resp, 503, "Service Unavailable");
            h2_response_set_content_type(h2resp, "application/json");
            h2_response_set_body(h2resp, draining_body, draining_len);
            h2_response_finalize(h2resp);
        } else {
            const char *headers =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: application/json\r\n"
                "Retry-After: 5\r\n"
                "Content-Length: 53\r\n"
                "\r\n";
            ssl_write_all(ssl, headers, strlen(headers));
            ssl_write_all(ssl, draining_body, draining_len);
        }
        
        return 0;
    }
    
    if (h2resp) {
        h2_response_init(h2resp);
        h2_response_set_status(h2resp, 200, "OK");
        h2_response_set_content_type(h2resp, "application/json");
        h2_response_set_body(h2resp, body, body_len);
        h2_response_finalize(h2resp);
    } else {
        const char *headers =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 15\r\n"
            "\r\n";
        ssl_write_all(ssl, headers, strlen(headers));
        ssl_write_all(ssl, body, body_len);
    }
    
    return 0;
}

static int is_health_check(const HttpRequest *req)
{
    return (strcmp(req->path, "/health") == 0);
}

static int ssl_write_all(SSL *ssl, const char *buf, size_t len)
{
    size_t total = 0;

    while (total < len)
    {
        int written = SSL_write(ssl, buf + total, (int)(len - total));
        if (written <= 0)
            return -1;
        total += (size_t)written;
    }

    return 0;
}

static int send_simple_response(SSL *ssl, const char *status_line,
                                const char *content_type, const char *body)
{
    size_t body_len = body ? strlen(body) : 0;
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "%s\r\n%sContent-Length: %zu\r\n\r\n",
                              status_line,
                              content_type ? content_type : "",
                              body_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header))
        return -1;
    if (ssl_write_all(ssl, header, (size_t)header_len) != 0)
        return -1;
    if (body_len > 0 && ssl_write_all(ssl, body, body_len) != 0)
        return -1;
    return 0;
}

static int populate_http2_response(Http2Response *resp, const char *body,
                                   int status_code, const char *status_text,
                                   const char *content_type)
{
    int body_len;

    if (!resp || !body || !status_text || !content_type)
        return -1;

    body_len = snprintf(resp->body, sizeof(resp->body), "%s", body);
    if (body_len < 0 || (size_t)body_len >= sizeof(resp->body))
        return -1;

    resp->body_len = (size_t)body_len;
    resp->status_code = status_code;
    snprintf(resp->status_text, sizeof(resp->status_text), "%s", status_text);
    snprintf(resp->content_type, sizeof(resp->content_type), "%s", content_type);
    resp->num_headers = 0;
    return 0;
}

static const char *guess_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (!ext)
        return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcmp(ext, ".json") == 0)
        return "application/json";
    if (strcmp(ext, ".txt") == 0)
        return "text/plain";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    return "application/octet-stream";
}

static StaticLookupResult lookup_static_file(const HttpRequest *req, ServerConfig *config,
                                             int *fd_out, off_t *filesize_out,
                                             const char **content_type_out)
{
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
                char root_real[PATH_MAX];
                char file_real[PATH_MAX];
                const char *root = config->routes[i].document_root;
                size_t root_len = strlen(root);
                bool has_slash;
                int fd;
                int written;

                if (root_len == 0)
                    return STATIC_LOOKUP_ERROR;

                has_slash = (root[root_len - 1] == '/');
                written = snprintf(filepath, sizeof(filepath),
                                   has_slash ? "%s%s" : "%s/%s",
                                   root,
                                   req->path + prefix_len);
                if (written < 0 || (size_t)written >= sizeof(filepath))
                    return STATIC_LOOKUP_ERROR;

                if (config->routes[i].document_root_resolved) {
                    strncpy(root_real, config->routes[i].document_root_real, sizeof(root_real) - 1);
                    root_real[sizeof(root_real) - 1] = '\0';
                } else if (!realpath(root, root_real)) {
                    return STATIC_LOOKUP_ERROR;
                }

                fd = open(filepath, O_RDONLY);
                if (fd < 0)
                    return STATIC_LOOKUP_NOT_FOUND;

                if (!realpath(filepath, file_real))
                {
                    close(fd);
                    return STATIC_LOOKUP_NOT_FOUND;
                }

                root_len = strlen(root_real);
                if (strncmp(file_real, root_real, root_len) != 0 ||
                    (file_real[root_len] != '\0' && file_real[root_len] != '/'))
                {
                    close(fd);
                    return STATIC_LOOKUP_FORBIDDEN;
                }

                *filesize_out = lseek(fd, 0, SEEK_END);
                if (*filesize_out < 0 || lseek(fd, 0, SEEK_SET) < 0)
                {
                    close(fd);
                    return STATIC_LOOKUP_ERROR;
                }

                *fd_out = fd;
                *content_type_out = guess_content_type(file_real);
                return STATIC_LOOKUP_READY;
            }
        }
    }

    return STATIC_LOOKUP_NO_ROUTE;
}

static int serve_static_h2(HttpRequest *req, ServerConfig *config, Http2Response *h2resp)
{
    int fd = -1;
    off_t filesize = 0;
    const char *content_type = "application/octet-stream";
    StaticLookupResult lookup =
        lookup_static_file(req, config, &fd, &filesize, &content_type);

    if (lookup == STATIC_LOOKUP_NO_ROUTE)
        return 1;
    if (lookup == STATIC_LOOKUP_NOT_FOUND)
        return populate_http2_response(h2resp, "", 404, "Not Found", "text/plain");
    if (lookup == STATIC_LOOKUP_FORBIDDEN)
        return populate_http2_response(h2resp, "", 403, "Forbidden", "text/plain");
    if (lookup == STATIC_LOOKUP_ERROR)
        return -1;

    if ((size_t)filesize > sizeof(h2resp->body))
    {
        close(fd);
        return populate_http2_response(h2resp, "", 413, "Payload Too Large", "text/plain");
    }

    h2_response_init(h2resp);
    h2_response_set_status(h2resp, 200, "OK");
    h2_response_set_content_type(h2resp, content_type);

    size_t total = 0;
    while (total < (size_t)filesize)
    {
        ssize_t n = read(fd, h2resp->body + total, (size_t)filesize - total);
        if (n < 0)
        {
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    close(fd);

    h2_response_set_body_len(h2resp, total);
    h2_response_finalize(h2resp);
    return 0;
}

static int has_matching_proxy_route(HttpRequest *req, ServerConfig *config)
{
    for (int i = 0; i < config->route_count; i++)
    {
        if (strcmp(config->routes[i].technology, "reverse_proxy") == 0)
        {
            size_t prefix_len = strlen(config->routes[i].path);
            if (strlen(req->path) >= prefix_len &&
                strncmp(req->path, config->routes[i].path, prefix_len) == 0)
                return 1;
        }
    }
    return 0;
}

/* serve_static_tls()
 *
 * If the HTTP request's path starts with a static route, constructs the full file path,
 * opens the file, and sends it to the client using SSL_write() and sendfile() for zero-copy.
 * If the file is not found, a 404 response is sent.
 */
int serve_static_tls(HttpRequest *req, ServerConfig *config, SSL *ssl)
{
    if (!req || !req->path || !config || !ssl)
        return -1;

    int fd = -1;
    off_t filesize = 0;
    const char *content_type = "application/octet-stream";
    StaticLookupResult lookup =
        lookup_static_file(req, config, &fd, &filesize, &content_type);

    if (lookup == STATIC_LOOKUP_NO_ROUTE)
        return -1;
    if (lookup == STATIC_LOOKUP_NOT_FOUND)
        return send_simple_response(ssl, "HTTP/1.1 404 Not Found", NULL, NULL);
    if (lookup == STATIC_LOOKUP_FORBIDDEN)
        return send_simple_response(ssl, "HTTP/1.1 403 Forbidden", NULL, NULL);
    if (lookup == STATIC_LOOKUP_ERROR)
        return -1;

    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
                              content_type, (long)filesize);
    if (header_len < 0 || (size_t)header_len >= sizeof(header) ||
        ssl_write_all(ssl, header, (size_t)header_len) != 0)
    {
        close(fd);
        return -1;
    }

    char filebuf[BUFFER_SIZE];
    ssize_t bytes;
    while ((bytes = read(fd, filebuf, sizeof(filebuf))) > 0)
    {
        if (ssl_write_all(ssl, filebuf, (size_t)bytes) != 0)
        {
            close(fd);
            return -1;
        }
    }
    if (bytes < 0)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
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
            if (ssl_write_all(ssl, buf, (size_t)n) != 0)
            {
                done = 1;
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
int proxy_request_tls(HttpRequest *req, const char *raw_request, size_t req_len, ServerConfig *config, SSL *ssl)
{
    if (!req || !req->path || !raw_request || !config || !ssl)
        return -1;

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
                size_t sent = 0;
                while (sent < req_len)
                {
                    ssize_t n = send(backend_fd, raw_request + sent, req_len - sent, 0);
                    if (n <= 0)
                        break;
                    sent += (size_t)n;
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
    (void)raw;
    (void)raw_len;
    
    static const char *root_body =
        "<html><head><title>High Performance Web Server</title></head>"
        "<body><h1>Welcome to High Performance Web Server</h1>"
        "<p>This server is designed to outperform Nginx and Apache by utilizing "
        "advanced I/O techniques, a modular architecture, and an efficient reverse proxy mechanism.</p>"
        "</body></html>";

    if (!req || !req->path)
        return -1;

    /* Health check endpoint - highest priority */
    if (is_health_check(req)) {
        return send_health_response(ssl, h2resp);
    }

    if (h2resp)
    {
        if (strcmp(req->path, "/") == 0)
            return populate_http2_response(h2resp, root_body, 200, "OK", "text/html");
        if (config)
        {
            int static_result = serve_static_h2(req, config, h2resp);
            if (static_result == 0)
                return 0;
            if (static_result < 0)
                return -1;

            if (has_matching_proxy_route(req, config))
                return populate_http2_response(h2resp, "", 501, "Not Implemented", "text/plain");
        }
        return populate_http2_response(h2resp, "", 404, "Not Found", "text/plain");
    }

    if (strcmp(req->path, "/") == 0)
        return send_simple_response(ssl, "HTTP/1.1 200 OK", "Content-Type: text/html\r\n",
                                    root_body);

    if (serve_static_tls(req, config, ssl) == 0)
        return 0;
    if (proxy_request_tls(req, raw, raw_len, config, ssl) == 0)
        return 0;

    if (send_simple_response(ssl, "HTTP/1.1 404 Not Found", NULL, NULL) != 0)
        return -1;
    return -1;
}
