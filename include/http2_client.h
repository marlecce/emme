#ifndef HTTP2_CLIENT_H
#define HTTP2_CLIENT_H

#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include "config.h"

#define HTTP2_CLIENT_MAX_HEADERS 32
#define HTTP2_CLIENT_BUFFER_SIZE 65536

typedef struct {
    SSL *ssl;
    int socket_fd;
    nghttp2_session *session;
    nghttp2_session_callbacks *callbacks;
    
    // Request state
    const char *method;
    const char *path;
    const char *host;
    const char *request_body;
    size_t request_body_len;
    size_t body_sent;
    
    // Response state
    char response_buffer[HTTP2_CLIENT_BUFFER_SIZE];
    size_t response_received;
    int response_status;
    char response_status_text[64];
    char response_headers[HTTP2_CLIENT_MAX_HEADERS][256];
    size_t response_header_count;
    
    // Connection state
    int want_read;
    int want_write;
    int done;
    int error_code;
} http2_client_t;

typedef struct {
    char host[256];
    int port;
    bool tls_enabled;
    bool tls_verify;
} backend_config_t;

// Lifecycle functions
int http2_client_init(http2_client_t *client, const backend_config_t *backend);
int http2_client_connect(http2_client_t *client, const backend_config_t *backend);
int http2_client_send_request(http2_client_t *client, const char *method, 
                               const char *path, const char *host,
                               const char *body, size_t body_len);
int http2_client_recv_response(http2_client_t *client);
void http2_client_cleanup(http2_client_t *client);

// Helper functions
const char* http2_client_get_response_body(http2_client_t *client);
size_t http2_client_get_response_length(http2_client_t *client);
int http2_client_get_response_status(http2_client_t *client);
bool http2_client_is_done(http2_client_t *client);
bool http2_client_has_error(http2_client_t *client);
int http2_client_get_error_code(http2_client_t *client);

#endif // HTTP2_CLIENT_H
