#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <liburing.h>
#include <sys/sendfile.h>
#include "config.h"
#include "server.h"
#include "http_parser.h"
#include "router.h"
#include "tls.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "thread_pool.h"
#include "log.h"
#include <ctype.h>
#include "http2_response.h"

SSL_CTX *ssl_ctx = NULL;

static void handle_http2_connection(SSL *ssl, int client_fd, ServerConfig *config);
static void handle_http1_connection(SSL *ssl, int client_fd, ServerConfig *config);

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data)
{
    (void)session;
    (void)flags;
    SSL *ssl = (SSL *)user_data;
    ssize_t ret = SSL_write(ssl, data, length);
    if (ret <= 0)
    {
        int ssl_error = SSL_get_error(ssl, ret);
        log_message(LOG_LEVEL_ERROR, "SSL_write failed in send_callback. Error: %d", ssl_error);
    }
    return ret;
}

static ssize_t recv_callback(nghttp2_session *session, uint8_t *buf, size_t length,
                             int flags, void *user_data)
{
    // Remove or lower verbosity for repetitive logs
    log_message(LOG_LEVEL_DEBUG, "recv_callback: start");
    (void)session;
    (void)flags;
    SSL *ssl = (SSL *)user_data;
    ssize_t ret = SSL_read(ssl, buf, length);
    if (ret <= 0)
    {
        int ssl_error = SSL_get_error(ssl, ret);
        if (ssl_error == SSL_ERROR_ZERO_RETURN)
        {
            log_message(LOG_LEVEL_INFO, "SSL connection closed by peer");
            return NGHTTP2_ERR_EOF;
        }
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
        {
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        log_message(LOG_LEVEL_ERROR, "SSL_read failed in recv_callback. Error: %d", ssl_error);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    log_message(LOG_LEVEL_DEBUG, "recv_callback: end, bytes read: %zd", ret);
    return ret;
}

static int on_header_callback(nghttp2_session *session,
                              const nghttp2_frame *frame,
                              const uint8_t *name, size_t namelen,
                              const uint8_t *value, size_t valuelen,
                              uint8_t flags, void *user_data)
{
    (void)session;
    (void)flags;
    (void)user_data;

    // Only for request headers.
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_REQUEST)
    {
        // Get or allocate per-stream data.
        StreamData *data = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (!data)
        {
            data = calloc(1, sizeof(StreamData));
            nghttp2_session_set_stream_user_data(session, frame->hd.stream_id, data);
        }
        // Process pseudo-headers.
        if (namelen >= 1 && name[0] == ':')
        {
            if (strncmp((const char *)name, ":method", namelen) == 0)
            {
                data->req.method = strndup((const char *)value, valuelen);
            }
            else if (strncmp((const char *)name, ":path", namelen) == 0)
            {
                data->req.path = strndup((const char *)value, valuelen);
            }
            else if (strncmp((const char *)name, ":scheme", namelen) == 0)
            {
                // ignore scheme
            }
            else if (strncmp((const char *)name, ":authority", namelen) == 0)
            {
                // Optionally store authority (or into version field if desired)
                data->req.version = strndup((const char *)value, valuelen);
            }
        }
    }
    return 0;
}
static ssize_t http2_body_read_callback(
    nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length,
    uint32_t *data_flags, nghttp2_data_source *source, void *user_data)
{
    (void)session;
    (void)stream_id;
    (void)user_data;
    struct
    {
        const char *data;
        size_t len;
        size_t sent;
    } *ctx = (void *)source->ptr;

    size_t remaining = ctx->len - ctx->sent;
    size_t to_copy = remaining < length ? remaining : length;
    if (to_copy > 0)
    {
        memcpy(buf, ctx->data + ctx->sent, to_copy);
        ctx->sent += to_copy;
    }
    if (ctx->sent >= ctx->len)
    {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
        free(ctx);
    }
    return to_copy;
}

static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data)
{
    (void)user_data;
    log_message(LOG_LEVEL_DEBUG, "on_frame_recv_callback: start");
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_REQUEST &&
        (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS))
    {
        StreamData *data = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);

        if (data)
        {
            // Synthesize a raw request line for compatibility with your router
            char raw_request[BUFFER_SIZE];
            snprintf(raw_request, sizeof(raw_request), "%s %s %s\r\n",
                     data->req.method ? data->req.method : "GET",
                     data->req.path ? data->req.path : "/",
                     data->req.version ? data->req.version : "HTTP/2");

            log_message(LOG_LEVEL_INFO, "Routing HTTP/2 request for path (synthesized): %s", data->req.path);

            // Prepare HTTP/2 response struct
            Http2Response h2resp = {0};

            // Call your router to fill h2resp
            // NOTE: Pass NULL for SSL, as it's not used for HTTP/2
            route_request_tls(&data->req, raw_request, strlen(raw_request), NULL, NULL, &h2resp);

            // If router didn't set headers, set defaults
            if (h2resp.num_headers == 0)
            {
                char status_str[4];
                snprintf(status_str, sizeof(status_str), "%d", h2resp.status_code ? h2resp.status_code : 200);
                h2resp.headers[0] = MAKE_NV(":status", status_str);
                h2resp.headers[1] = MAKE_NV("content-type", h2resp.content_type[0] ? h2resp.content_type : "text/plain");
                char clen[32];
                snprintf(clen, sizeof(clen), "%zu", h2resp.body_len);
                h2resp.headers[2] = MAKE_NV("content-length", clen);
                h2resp.num_headers = 3;
            }

            // Always ensure body is not empty
            if (h2resp.body_len == 0)
            {
                static const char dummy_body[] = "\n";
                strncpy(h2resp.body, dummy_body, sizeof(h2resp.body) - 1);
                h2resp.body[sizeof(h2resp.body) - 1] = '\0';
                h2resp.body_len = strlen(h2resp.body);
            }

            if (h2resp.status_code == 0)
            {
                h2resp.status_code = 200;
            }

            // Data provider for nghttp2
            nghttp2_data_provider data_prd;
            struct
            {
                const char *data;
                size_t len;
                size_t sent;
            } *body_ctx = malloc(sizeof(*body_ctx));

            body_ctx->data = h2resp.body;
            body_ctx->len = h2resp.body_len;
            body_ctx->sent = 0;

            data_prd.source.ptr = body_ctx;
            data_prd.read_callback = http2_body_read_callback;

            int rv = nghttp2_submit_response(session, frame->hd.stream_id,
                                             h2resp.headers, h2resp.num_headers,
                                             &data_prd);
            if (rv != 0)
            {
                log_message(LOG_LEVEL_ERROR, "nghttp2_submit_response failed: %s", nghttp2_strerror(rv));
            }
            else
            {
                log_message(LOG_LEVEL_INFO, "nghttp2_submit_response succeeded for stream %d", frame->hd.stream_id);
            }
        }
    }
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                    uint32_t error_code, void *user_data)
{
    (void)error_code;
    (void)user_data;
    StreamData *data = nghttp2_session_get_stream_user_data(session, stream_id);
    if (data)
    {
        free((void *)data->req.method);
        free((void *)data->req.path);
        free((void *)data->req.version);
        free(data);
        nghttp2_session_set_stream_user_data(session, stream_id, NULL);
    }

    return 0;
}

static void handle_http2_connection(SSL *ssl, int client_fd, ServerConfig *config)
{
    (void)client_fd;
    (void)config;

    int rv;

    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    nghttp2_session_callbacks *callbacks;
    nghttp2_session *session;

    if (nghttp2_session_callbacks_new(&callbacks) != 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to initialize HTTP/2 callbacks");
        return;
    }

    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_recv_callback(callbacks, recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);

    if (nghttp2_session_server_new(&session, callbacks, ssl) != 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to create HTTP/2 session");
        nghttp2_session_callbacks_del(callbacks);
        return;
    }

    log_message(LOG_LEVEL_INFO, "HTTP/2 session started");

    // Send the initial SETTINGS frame
    rv = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0);
    if (rv < 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to send initial SETTINGS frame: %s", nghttp2_strerror(rv));
        nghttp2_session_del(session);
        nghttp2_session_callbacks_del(callbacks);
        return;
    }

    while (nghttp2_session_want_read(session) || nghttp2_session_want_write(session))
    {
        rv = nghttp2_session_recv(session);
        if (rv < 0)
        {
            if (rv == NGHTTP2_ERR_EOF)
            {
                log_message(LOG_LEVEL_INFO, "HTTP/2 session closed by client");
                break;
            }
            log_message(LOG_LEVEL_ERROR, "HTTP/2 session recv error: %s", nghttp2_strerror(rv));
            break;
        }

        rv = nghttp2_session_send(session);
        if (rv < 0)
        {
            log_message(LOG_LEVEL_ERROR, "HTTP/2 session send error: %s", nghttp2_strerror(rv));
            break;
        }
    }

    if (SSL_get_shutdown(ssl) & SSL_RECEIVED_SHUTDOWN)
    {
        SSL_shutdown(ssl);
    }

    nghttp2_session_del(session);
    nghttp2_session_callbacks_del(callbacks);
}

static void handle_http1_connection(SSL *ssl, int client_fd, ServerConfig *config)
{
    (void)client_fd;
    char buffer[BUFFER_SIZE];
    int total_read = 0;

    while (total_read < BUFFER_SIZE - 1)
    {
        int n = SSL_read(ssl, buffer + total_read, BUFFER_SIZE - 1 - total_read);
        if (n <= 0)
        {
            int ssl_error = SSL_get_error(ssl, n);
            log_message(LOG_LEVEL_ERROR, "SSL_read failed. Error: %d", ssl_error);
            return;
        }
        total_read += n;

        if (strstr(buffer, "\r\n\r\n") != NULL)
        {
            break;
        }
    }
    buffer[total_read] = '\0';

    HttpRequest req;
    if (parse_http_request(buffer, total_read, &req) != 0)
    {
        const char *bad_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        SSL_write(ssl, bad_response, strlen(bad_response));
        log_message(LOG_LEVEL_WARN, "Invalid HTTP request received.");
        return;
    }

    log_message(LOG_LEVEL_INFO, "Valid HTTP request received. Routing...");
    route_request_tls(&req, buffer, total_read, config, ssl, NULL);
}

void client_task(void *arg)
{
    ClientTaskData *data = (ClientTaskData *)arg;
    handle_client(data->client_fd, data->config);
    free(data); // Free the allocated memory after processing.
}

void handle_client(int client_fd, ServerConfig *config)
{
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    SSL *ssl = SSL_new(ssl_ctx);
    if (!ssl)
    {
        close(client_fd);
        return;
    }

    SSL_set_fd(ssl, client_fd);

    SSL_set_app_data(ssl, config);

    if (SSL_accept(ssl) <= 0)
    {
        log_message(LOG_LEVEL_ERROR, "SSL accept failed");
        SSL_free(ssl);
        close(client_fd);
        return;
    }

    const unsigned char *alpn_proto = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn_proto, &alpn_len);

    if (alpn_len == 2 && memcmp(alpn_proto, "h2", 2) == 0)
    {
        log_message(LOG_LEVEL_INFO, "Negotiated HTTP/2");
        handle_http2_connection(ssl, client_fd, config);
    }
    else
    {
        log_message(LOG_LEVEL_INFO, "Negotiated HTTP/1.1");
        handle_http1_connection(ssl, client_fd, config);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
}

int start_server(ServerConfig *config)
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct io_uring ring;

    // Create and initialize io_uring for accepting connections.
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) != 0)
    {
        perror("io_uring_queue_init failed");
        return 1;
    }

    // Create the server socket.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("Socket creation error");
        return 1;
    }

    // Bind the socket to the address and port.
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Bind error");
        close(server_fd);
        return 1;
    }

    // Start listening for incoming connections.
    if (listen(server_fd, 2048) == -1)
    {
        perror("Listen error");
        close(server_fd);
        return 1;
    }

    // Initialize SSL context.
    ssl_ctx = create_ssl_context(config->ssl.certificate, config->ssl.private_key);

    // Create the dynamic thread pool.
    ThreadPool *pool = thread_pool_create(32, config->max_connections);
    if (!pool)
    {
        fprintf(stderr, "Failed to create thread pool\n");
        close(server_fd);
        return 1;
    }
    printf("Emme listening on port %d...\n", config->port);

    // Main loop using io_uring to accept new connections
    while (1)
    {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe)
        {
            fprintf(stderr, "Failed to get submission queue entry\n");
            break;
        }

        io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)&client_addr, &client_len, 0);
        if (io_uring_submit(&ring) < 0)
        {
            perror("io_uring_submit failed");
            break;
        }

        struct io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&ring, &cqe) < 0)
        {
            perror("io_uring_wait_cqe failed");
            break;
        }
        client_fd = cqe->res;
        io_uring_cqe_seen(&ring, cqe);

        // Set client_fd to blocking mode (if needed)
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);

        // Allocate and prepare a ClientTaskData structure.
        ClientTaskData *task_data = malloc(sizeof(ClientTaskData));
        if (!task_data)
        {
            perror("Failed to allocate memory for client task");
            close(client_fd);
            continue;
        }
        task_data->client_fd = client_fd;
        task_data->config = config;

        // Add the client handling task to the thread pool.
        if (!thread_pool_add_task(pool, client_task, task_data))
        {
            fprintf(stderr, "Failed to add task to thread pool\n");
            free(task_data);
            close(client_fd);
        }
    }

    // Cleanup resources on server shutdown.
    thread_pool_destroy(pool);
    close(server_fd);
    io_uring_queue_exit(&ring);
    cleanup_ssl_context(ssl_ctx);
    return 0;
}
