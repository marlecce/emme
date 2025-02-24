/* router.c - Routing module for the web server
 *
 * This module examines the HTTP request (HttpRequest) and, based on the routes
 * defined in the configuration (ServerConfig), decides whether to:
 *   - Serve a static file (using sendfile for zero-copy).
 *   - Forward the request to a backend (reverse proxy).
 *
 * In the case of a reverse proxy, a connection to the backend is established,
 * the request is forwarded, and a bidirectional loop is activated to transfer
 * data in both directions.
 *
 * Additionally, if the request path is "/" (the root), a default HTML page is
 * returned which presents the project and its main features.
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
 #include "router.h"
 
 #define BUFFER_SIZE 1024
 
 /* serve_static()
  *
  * If the HTTP request's path starts with the prefix defined for a route with
  * technology "static", this function constructs the full file path (document_root + remaining part of the path),
  * opens the file, and sends it to the client using sendfile() (zero-copy).
  * If the file is not found, a 404 response is sent.
  */
 int serve_static(HttpRequest *req, ServerConfig *config, int client_fd) {
     // Check that req->path is not NULL
     if (!req || !req->path) {
         return -1;
     }
     
     for (int i = 0; i < config->route_count; i++) {
         if (strcmp(config->routes[i].technology, "static") == 0) {
             size_t prefix_len = strlen(config->routes[i].path);
             // If the request path is shorter than the route prefix, skip this route.
             if (strlen(req->path) < prefix_len) {
                 continue;
             }
             // Check if the request path starts with the route prefix.
             if (strncmp(req->path, config->routes[i].path, prefix_len) == 0) {
                 char filepath[512];
                 // Construct the full path: document_root + (remaining part of the path)
                 if ((size_t)snprintf(filepath, sizeof(filepath), "%s%s", 
                                      config->routes[i].document_root, req->path + prefix_len) >= sizeof(filepath)) {
                     fprintf(stderr, "serve_static: Path too long\n");
                     return -1;
                 }
                 int fd = open(filepath, O_RDONLY);
                 if (fd < 0) {
                     const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                     send(client_fd, not_found, strlen(not_found), 0);
                     return -1;
                 }
                 off_t filesize = lseek(fd, 0, SEEK_END);
                 lseek(fd, 0, SEEK_SET);
                 char header[256];
                 snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", filesize);
                 send(client_fd, header, strlen(header), 0);
                 off_t offset = 0;
                 sendfile(client_fd, fd, &offset, filesize);
                 close(fd);
                 return 0;
             }
         }
     }
     return -1;
 }
 
 /* set_nonblocking()
  *
  * Sets the file descriptor fd to non-blocking mode.
  */
 int set_nonblocking(int fd) {
     int flags = fcntl(fd, F_GETFL, 0);
     if (flags == -1) return -1;
     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
 }
 
 /* proxy_bidirectional()
  *
  * Starts a bidirectional loop to transfer data between the backend and the client,
  * using poll() to monitor both sockets.
  * This function continues transferring data until one of the two connections is closed.
  */
 int proxy_bidirectional(int backend_fd, int client_fd) {
     if (set_nonblocking(backend_fd) < 0 || set_nonblocking(client_fd) < 0) {
         return -1;
     }
     struct pollfd fds[2];
     fds[0].fd = backend_fd;
     fds[0].events = POLLIN;
     fds[1].fd = client_fd;
     fds[1].events = POLLIN;
 
     char buf[BUFFER_SIZE];
     int done = 0;
     while (!done) {
         int ret = poll(fds, 2, 3000);
         if (ret <= 0) break;
         if (fds[0].revents & POLLIN) {
             int n = read(backend_fd, buf, sizeof(buf));
             if (n > 0) {
                 int sent = 0;
                 while (sent < n) {
                     int s = send(client_fd, buf + sent, n - sent, 0);
                     if (s < 0) break;
                     sent += s;
                 }
             } else {
                 done = 1;
             }
         }
         if (fds[1].revents & POLLIN) {
             int n = read(client_fd, buf, sizeof(buf));
             if (n > 0) {
                 int sent = 0;
                 while (sent < n) {
                     int s = send(backend_fd, buf + sent, n - sent, 0);
                     if (s < 0) break;
                     sent += s;
                 }
             } else {
                 done = 1;
             }
         }
     }
     return 0;
 }
 
 /* proxy_request()
  *
  * Implements full request forwarding to the backend for reverse_proxy requests.
  * It searches the configured routes for one that matches the request path.
  * - Extracts the IP address and port from the backend field (format "IP:PORT").
  * - Opens a TCP connection to the backend and forwards the entire HTTP request (raw_request).
  * - Starts proxy_bidirectional() to transfer data between backend and client bidirectionally.
  */
 int proxy_request(HttpRequest *req, char *raw_request, int req_len, ServerConfig *config, int client_fd) {
     for (int i = 0; i < config->route_count; i++) {
         if (strcmp(config->routes[i].technology, "reverse_proxy") == 0) {
             size_t prefix_len = strlen(config->routes[i].path);
             // Ensure the request path is long enough before comparing
             if (strlen(req->path) < prefix_len) {
                 continue;
             }
             if (strncmp(req->path, config->routes[i].path, prefix_len) == 0) {
                 char ip[64];
                 int port;
                 if (sscanf(config->routes[i].backend, "%63[^:]:%d", ip, &port) != 2) {
                     return -1;
                 }
                 int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
                 if (backend_fd < 0) return -1;
                 struct sockaddr_in backend_addr;
                 memset(&backend_addr, 0, sizeof(backend_addr));
                 backend_addr.sin_family = AF_INET;
                 backend_addr.sin_port = htons(port);
                 if (inet_pton(AF_INET, ip, &backend_addr.sin_addr) <= 0) {
                     close(backend_fd);
                     return -1;
                 }
                 if (connect(backend_fd, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0) {
                     close(backend_fd);
                     return -1;
                 }
                 // Forward the entire HTTP request to the backend
                 int sent = 0;
                 while (sent < req_len) {
                     int n = send(backend_fd, raw_request + sent, req_len - sent, 0);
                     if (n <= 0) break;
                     sent += n;
                 }
                 // Start bidirectional forwarding between backend and client
                 proxy_bidirectional(backend_fd, client_fd);
                 close(backend_fd);
                 return 0;
             }
         }
     }
     return -1;
 }
 
 /* route_request()
  *
  * Routing function that decides how to handle the request:
  * - If the request path is "/", it serves a default HTML page.
  * - Otherwise, it first tries to serve the request as a static file (serve_static()).
  * - If that fails, it attempts to forward the request to the backend via reverse proxy (proxy_request()).
  * raw_request and req_len represent the buffer containing the full HTTP request.
  */
 int route_request(HttpRequest *req, char *raw_request, int req_len, ServerConfig *config, int client_fd) {
     // Serve default page if the request is for the root "/"
     if (strcmp(req->path, "/") == 0) {
         const char *html_content = "<html><head><title>High Performance Web Server</title></head>"
                                    "<body><h1>Welcome to High Performance Web Server</h1>"
                                    "<p>This server is designed to outperform Nginx and Apache by utilizing "
                                    "advanced I/O techniques, a modular architecture, and an efficient reverse proxy mechanism.</p>"
                                    "</body></html>";
         size_t content_length = strlen(html_content);
         char header[256];
         snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n", content_length);
         send(client_fd, header, strlen(header), 0);
         send(client_fd, html_content, content_length, 0);
         return 0;
     }
     
     if (serve_static(req, config, client_fd) == 0)
         return 0;
     if (proxy_request(req, raw_request, req_len, config, client_fd) == 0)
         return 0;
     
     const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
     send(client_fd, not_found, strlen(not_found), 0);
     return -1;
 }
 