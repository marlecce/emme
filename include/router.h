#ifndef ROUTER_H
#define ROUTER_H

#include "config.h"
#include "http2_response.h"
#include "http_parser.h" 
#include <openssl/ssl.h>
 
int serve_static_tls(HttpRequest *req, ServerConfig *config, SSL *ssl);
int proxy_bidirectional_tls(SSL *ssl, int backend_fd);
int proxy_request_tls(HttpRequest *req, char *raw_request, int req_len, ServerConfig *config, SSL *ssl);
int route_request_tls(HttpRequest *req, const char *raw, size_t raw_len, ServerConfig *config, SSL *ssl, Http2Response *h2resp);
#endif
