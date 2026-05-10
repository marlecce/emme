/* http2_client.c - HTTP/2 client implementation using nghttp2
 *
 * This module implements an HTTP/2 client for making requests to upstream backends.
 * It handles:
 *   - TLS connection establishment
 *   - nghttp2 session management (client mode)
 *   - HTTP/2 request/response framing
 *   - Stream multiplexing (single request per client for simplicity)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "http2_client.h"
#include "log.h"
#include "tls.h"
#include "metrics.h"

#define HTTP2_ALPN "h2"
#define HTTP2_ALPN_LEN 2
#define HTTP2_POLL_TIMEOUT_MS 5000

#define MAKE_NV(NAME, VALUE) \
    (nghttp2_nv){(uint8_t *)(NAME), (uint8_t *)(VALUE), strlen(NAME), strlen(VALUE), NGHTTP2_NV_FLAG_NONE}
#define HTTP2_ALPN_LEN 2
#define HTTP2_POLL_TIMEOUT_MS 5000

#ifndef DEBUG_H2C
#define DEBUG_H2C 0
#endif

#define H2C_LOG(...)                         \
    do {                                     \
        if (DEBUG_H2C)                       \
            log_message(LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } while (0)

// nghttp2 callback: send data
static ssize_t http2_client_send_callback(nghttp2_session *session,
                                           const uint8_t *data, size_t length,
                                           int flags, void *user_data)
{
    http2_client_t *client = (http2_client_t *)user_data;
    (void)session;
    (void)flags;
    
    int n = SSL_write(client->ssl, data, (int)length);
    if (n <= 0) {
        int ssl_err = SSL_get_error(client->ssl, n);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            client->want_read = 1;
            return NGHTTP2_ERR_WOULDBLOCK;
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            client->want_write = 1;
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        log_message(LOG_LEVEL_ERROR, "HTTP/2 client SSL_write failed: %d", ssl_err);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    
    H2C_LOG("http2_client: sent %zu bytes", (size_t)n);
    return n;
}

// nghttp2 callback: receive data
static ssize_t http2_client_recv_callback(nghttp2_session *session,
                                           uint8_t *buf, size_t length,
                                           int flags, void *user_data)
{
    http2_client_t *client = (http2_client_t *)user_data;
    (void)session;
    (void)flags;
    
    int n = SSL_read(client->ssl, buf, (int)length);
    if (n <= 0) {
        int ssl_err = SSL_get_error(client->ssl, n);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            client->want_read = 1;
            return NGHTTP2_ERR_WOULDBLOCK;
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            client->want_write = 1;
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        if (ssl_err == SSL_ERROR_ZERO_RETURN) {
            return NGHTTP2_ERR_EOF;
        }
        log_message(LOG_LEVEL_ERROR, "HTTP/2 client SSL_read failed: %d", ssl_err);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    
    H2C_LOG("http2_client: received %d bytes", n);
    return n;
}

// nghttp2 callback: on frame received
static int http2_client_on_frame_recv(nghttp2_session *session,
                                       const nghttp2_frame *frame,
                                       void *user_data)
{
    http2_client_t *client = (http2_client_t *)user_data;
    (void)session;
    
    if (frame->hd.type == NGHTTP2_HEADERS && 
        frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
        client->response_status = frame->headers.cat;
        H2C_LOG("http2_client: received HEADERS, status=%d", client->response_status);
    } else if (frame->hd.type == NGHTTP2_DATA) {
        H2C_LOG("http2_client: received DATA frame, length=%d", frame->hd.length);
    }
    
    return 0;
}

// nghttp2 callback: on data chunk received
static int http2_client_on_data_chunk_recv(nghttp2_session *session,
                                            uint8_t flags, int32_t stream_id,
                                            const uint8_t *data, size_t len,
                                            void *user_data)
{
    http2_client_t *client = (http2_client_t *)user_data;
    (void)session;
    (void)flags;
    (void)stream_id;
    
    if (client->response_received + len <= HTTP2_CLIENT_BUFFER_SIZE) {
        memcpy(client->response_buffer + client->response_received, data, len);
        client->response_received += len;
        H2C_LOG("http2_client: accumulated %zu bytes", client->response_received);
    } else {
        log_message(LOG_LEVEL_ERROR, "HTTP/2 client response buffer overflow");
        return -1;
    }
    
    return 0;
}

// nghttp2 callback: on stream close
static int http2_client_on_stream_close(nghttp2_session *session, int32_t stream_id,
                                         uint32_t error_code, void *user_data)
{
    http2_client_t *client = (http2_client_t *)user_data;
    (void)session;
    (void)stream_id;
    (void)error_code;
    
    client->done = 1;
    client->error_code = error_code;
    H2C_LOG("http2_client: stream closed, error_code=%u", error_code);
    
    return 0;
}

// nghttp2 callback: data provider read
static ssize_t http2_client_data_read(nghttp2_session *session, int32_t stream_id,
                                       uint8_t *buf, size_t length,
                                       uint32_t *data_flags,
                                       nghttp2_data_source *source,
                                       void *user_data)
{
    http2_client_t *client = (http2_client_t *)user_data;
    (void)session;
    (void)stream_id;
    (void)source;
    
    size_t remaining = client->request_body_len - client->body_sent;
    size_t to_send = (remaining < length) ? remaining : length;
    
    if (to_send > 0) {
        memcpy(buf, client->request_body + client->body_sent, to_send);
        client->body_sent += to_send;
        H2C_LOG("http2_client: sent %zu body bytes", to_send);
    }
    
    if (remaining <= length) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    
    return (ssize_t)to_send;
}

// Create SSL context for HTTP/2 client
static SSL_CTX* create_http2_client_ssl_ctx(bool verify_certs)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        log_message(LOG_LEVEL_ERROR, "Failed to create SSL context for HTTP/2 client");
        return NULL;
    }
    
    // Set ALPN protocols
    unsigned char alpn_protos[] = {
        2, 'h', '2'
    };
    SSL_CTX_set_alpn_protos(ctx, alpn_protos, sizeof(alpn_protos));
    
    // Configure certificate verification
    if (verify_certs) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(ctx);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }
    
    return ctx;
}

// Connect to backend server
static int connect_to_backend(const char *host, int port)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create socket for backend %s:%d", host, port);
        return -1;
    }
    
    // Set non-blocking
    int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        // Try DNS resolution
        struct hostent *he = gethostbyname(host);
        if (!he) {
            log_message(LOG_LEVEL_ERROR, "Failed to resolve backend host: %s", host);
            close(sock_fd);
            return -1;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    int ret = connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        log_message(LOG_LEVEL_ERROR, "Failed to connect to backend %s:%d: %s", 
                    host, port, strerror(errno));
        close(sock_fd);
        return -1;
    }
    
    return sock_fd;
}

// Initialize HTTP/2 client callbacks
static int init_client_callbacks(http2_client_t *client)
{
    if (nghttp2_session_callbacks_new(&client->callbacks) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create HTTP/2 client callbacks");
        return -1;
    }
    
    nghttp2_session_callbacks_set_send_callback(client->callbacks, http2_client_send_callback);
    nghttp2_session_callbacks_set_recv_callback(client->callbacks, http2_client_recv_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(client->callbacks, http2_client_on_frame_recv);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(client->callbacks, http2_client_on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(client->callbacks, http2_client_on_stream_close);
    
    return 0;
}

int http2_client_init(http2_client_t *client, const backend_config_t *backend)
{
    (void)backend;
    
    if (!client) {
        return -1;
    }
    
    memset(client, 0, sizeof(http2_client_t));
    client->socket_fd = -1;
    
    if (init_client_callbacks(client) != 0) {
        return -1;
    }
    
    return 0;
}

int http2_client_connect(http2_client_t *client, const backend_config_t *backend)
{
    if (!client || !backend) {
        return -1;
    }
    
    // Connect to backend
    client->socket_fd = connect_to_backend(backend->host, backend->port);
    if (client->socket_fd < 0) {
        return -1;
    }
    
    // Create SSL context
    SSL_CTX *ssl_ctx = create_http2_client_ssl_ctx(backend->tls_verify);
    if (!ssl_ctx) {
        close(client->socket_fd);
        return -1;
    }
    
    // Create SSL object
    client->ssl = SSL_new(ssl_ctx);
    if (!client->ssl) {
        log_message(LOG_LEVEL_ERROR, "Failed to create SSL object for HTTP/2 client");
        SSL_CTX_free(ssl_ctx);
        close(client->socket_fd);
        return -1;
    }
    
    SSL_set_fd(client->ssl, client->socket_fd);
    SSL_set_connect_state(client->ssl);
    
    // Perform non-blocking handshake
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    int ret = SSL_connect(client->ssl);
    while (ret <= 0) {
        int err = SSL_get_error(client->ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            struct pollfd pfd = {
                .fd = client->socket_fd,
                .events = (err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT
            };
            int poll_ret = poll(&pfd, 1, HTTP2_POLL_TIMEOUT_MS);
            if (poll_ret <= 0) {
                log_message(LOG_LEVEL_ERROR, "HTTP/2 client TLS handshake timeout");
                SSL_free(client->ssl);
                SSL_CTX_free(ssl_ctx);
                close(client->socket_fd);
                return -1;
            }
            ret = SSL_connect(client->ssl);
        } else {
            log_message(LOG_LEVEL_ERROR, "HTTP/2 client TLS handshake failed: %d", err);
            SSL_free(client->ssl);
            SSL_CTX_free(ssl_ctx);
            close(client->socket_fd);
            return -1;
        }
    }
    
    gettimeofday(&end, NULL);
    long handshake_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                        (end.tv_usec - start.tv_usec) / 1000;
    log_message(LOG_LEVEL_INFO, "HTTP/2 client TLS handshake completed in %ld ms", handshake_ms);
    metrics_increment_tls_handshake(1);
    metrics_record_tls_handshake_duration(handshake_ms / 1000.0);
    
    // Verify ALPN negotiation
    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(client->ssl, &alpn, &alpn_len);
    if (!alpn || alpn_len != 2 || memcmp(alpn, "h2", 2) != 0) {
        log_message(LOG_LEVEL_ERROR, "HTTP/2 ALPN negotiation failed");
        SSL_free(client->ssl);
        SSL_CTX_free(ssl_ctx);
        close(client->socket_fd);
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "HTTP/2 client connected to %s:%d (ALPN: h2)", 
                backend->host, backend->port);
    
    // Create nghttp2 session (client mode)
    nghttp2_option *options;
    if (nghttp2_option_new(&options) == 0) {
        nghttp2_option_set_peer_max_concurrent_streams(options, 100);
    }
    
    if (nghttp2_session_client_new2(&client->session, client->callbacks, 
                                     client, options) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create HTTP/2 client session");
        nghttp2_option_del(options);
        SSL_free(client->ssl);
        SSL_CTX_free(ssl_ctx);
        close(client->socket_fd);
        return -1;
    }
    
    if (options) nghttp2_option_del(options);
    
    // Send client connection preface and SETTINGS
    if (nghttp2_submit_settings(client->session, NGHTTP2_FLAG_NONE, NULL, 0) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to submit HTTP/2 SETTINGS");
        http2_client_cleanup(client);
        return -1;
    }
    
    if (nghttp2_session_send(client->session) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to send HTTP/2 client preface");
        http2_client_cleanup(client);
        return -1;
    }
    
    return 0;
}

int http2_client_send_request(http2_client_t *client, const char *method,
                               const char *path, const char *host,
                               const char *body, size_t body_len)
{
    if (!client || !client->session || !method || !path || !host) {
        return -1;
    }
    
    client->method = method;
    client->path = path;
    client->host = host;
    client->request_body = body;
    client->request_body_len = body_len;
    client->body_sent = 0;
    
    // Build HTTP/2 headers
    nghttp2_nv headers[HTTP2_CLIENT_MAX_HEADERS];
    size_t num_headers = 0;
    
    // :method
    headers[num_headers++] = MAKE_NV(":method", method);
    
    // :path
    headers[num_headers++] = MAKE_NV(":path", path);
    
    // :authority
    headers[num_headers++] = MAKE_NV(":authority", host);
    
    // :scheme
    headers[num_headers++] = MAKE_NV(":scheme", "https");
    
    // User-Agent
    headers[num_headers++] = MAKE_NV("user-agent", "emme-http2-client/1.0");
    
    // Content-Type (if body present)
    if (body && body_len > 0) {
        headers[num_headers++] = MAKE_NV("content-type", "application/json");
    }
    
    // Submit request
    nghttp2_data_provider data_prd = {0};
    if (body && body_len > 0) {
        data_prd.source.ptr = NULL;
        data_prd.read_callback = http2_client_data_read;
    }
    
    int stream_id = nghttp2_submit_request(client->session, NULL, headers, 
                                            num_headers, body ? &data_prd : NULL, 
                                            client);
    if (stream_id < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to submit HTTP/2 request: %s", 
                    nghttp2_strerror(stream_id));
        return -1;
    }
    
    H2C_LOG("http2_client: submitted request on stream %d", stream_id);
    
    // Send the request
    if (nghttp2_session_send(client->session) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to send HTTP/2 request");
        return -1;
    }
    
    return stream_id;
}

int http2_client_recv_response(http2_client_t *client)
{
    if (!client || !client->session) {
        return -1;
    }
    
    struct timeval start = {0};
    gettimeofday(&start, NULL);
    
    while (!client->done) {
        // Check timeout (30 seconds)
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + 
                          (now.tv_usec - start.tv_usec) / 1000;
        if (elapsed_ms > 30000) {
            log_message(LOG_LEVEL_ERROR, "HTTP/2 client response timeout");
            return -1;
        }
        
        client->want_read = 0;
        client->want_write = 0;
        
        // Check if session wants read/write
        short events = 0;
        if (nghttp2_session_want_read(client->session)) events |= POLLIN;
        if (nghttp2_session_want_write(client->session)) events |= POLLOUT;
        
        if (events == 0) break;
        
        struct pollfd pfd = {
            .fd = client->socket_fd,
            .events = events
        };
        
        int poll_ret = poll(&pfd, 1, 100); // 100ms poll timeout
        if (poll_ret < 0) {
            log_message(LOG_LEVEL_ERROR, "HTTP/2 client poll failed: %s", strerror(errno));
            return -1;
        }
        if (poll_ret == 0) continue;
        
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            log_message(LOG_LEVEL_ERROR, "HTTP/2 client socket error");
            return -1;
        }
        
        // Receive data
        if (pfd.revents & POLLIN) {
            int ret = nghttp2_session_recv(client->session);
            if (ret < 0 && ret != NGHTTP2_ERR_WOULDBLOCK) {
                log_message(LOG_LEVEL_ERROR, "HTTP/2 session recv failed: %s", 
                            nghttp2_strerror(ret));
                return -1;
            }
        }
        
        // Send data
        if (pfd.revents & POLLOUT) {
            int ret = nghttp2_session_send(client->session);
            if (ret < 0 && ret != NGHTTP2_ERR_WOULDBLOCK) {
                log_message(LOG_LEVEL_ERROR, "HTTP/2 session send failed: %s", 
                            nghttp2_strerror(ret));
                return -1;
            }
        }
    }
    
    return client->response_status;
}

void http2_client_cleanup(http2_client_t *client)
{
    if (!client) return;
    
    if (client->session) {
        nghttp2_session_del(client->session);
        client->session = NULL;
    }
    
    if (client->callbacks) {
        nghttp2_session_callbacks_del(client->callbacks);
        client->callbacks = NULL;
    }
    
    if (client->ssl) {
        SSL *ssl = client->ssl;
        SSL_CTX *ctx = SSL_get_SSL_CTX(ssl);
        SSL_free(ssl);
        if (ctx) SSL_CTX_free(ctx);
        client->ssl = NULL;
    }
    
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
}

const char* http2_client_get_response_body(http2_client_t *client)
{
    if (!client) return NULL;
    return client->response_buffer;
}

size_t http2_client_get_response_length(http2_client_t *client)
{
    if (!client) return 0;
    return client->response_received;
}

int http2_client_get_response_status(http2_client_t *client)
{
    if (!client) return 0;
    return client->response_status;
}

bool http2_client_is_done(http2_client_t *client)
{
    if (!client) return true;
    return client->done;
}

bool http2_client_has_error(http2_client_t *client)
{
    if (!client) return true;
    return client->error_code != 0 || client->response_status == 0;
}

int http2_client_get_error_code(http2_client_t *client)
{
    if (!client) return -1;
    return client->error_code;
}
