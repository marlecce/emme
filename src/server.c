#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <liburing.h>
#include <sys/sendfile.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <stdatomic.h>
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

#ifndef DEBUG_H2
#define DEBUG_H2 0
#endif

#define H2_LOG(...)                         \
    do {                                    \
        if (DEBUG_H2)                       \
            log_message(LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } while (0)

#define H2_POLL_TIMEOUT_MS 100

typedef struct {
    SSL *ssl;
    ServerConfig *config;
    int want_read;
    int want_write;
    size_t total_read;
} H2IO;

static int find_header_end(const char *buf, size_t len)
{
    if (len < 4)
        return -1;
    for (size_t i = 0; i + 3 < len; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return (int)(i + 4);
    }
    return -1;
}

/* Callback invoked for each header received in an HTTP/2 frame */
static int on_header_callback(nghttp2_session *session,
                              const nghttp2_frame *frame,
                              const uint8_t *name, size_t namelen,
                              const uint8_t *value, size_t valuelen,
                              uint8_t flags, void *user_data)
{
    (void)session;
    (void)flags;
    (void)user_data;
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_REQUEST)
    {
        StreamData *data = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (!data)
        {
            data = calloc(1, sizeof(StreamData));
            if (!data)
            {
                log_message(LOG_LEVEL_ERROR, "Failed to allocate HTTP/2 stream state");
                return NGHTTP2_ERR_CALLBACK_FAILURE;
            }
            data->req.method = NULL;
            data->req.path = NULL;
            data->req.version = strdup("HTTP/2");
            if (!data->req.version)
            {
                free(data);
                log_message(LOG_LEVEL_ERROR, "Failed to allocate HTTP/2 request version");
                return NGHTTP2_ERR_CALLBACK_FAILURE;
            }
            nghttp2_session_set_stream_user_data(session, frame->hd.stream_id, data);
        }
        if (namelen >= 1 && name[0] == ':')
        {
            if (strncmp((const char *)name, ":method", namelen) == 0)
            {
                free((void *)data->req.method);
                data->req.method = strndup((const char *)value, valuelen);
                if (!data->req.method)
                {
                    log_message(LOG_LEVEL_ERROR, "Failed to allocate HTTP/2 method");
                    return NGHTTP2_ERR_CALLBACK_FAILURE;
                }
            }
            else if (strncmp((const char *)name, ":path", namelen) == 0)
            {
                free((void *)data->req.path);
                data->req.path = strndup((const char *)value, valuelen);
                if (!data->req.path)
                {
                    log_message(LOG_LEVEL_ERROR, "Failed to allocate HTTP/2 path");
                    return NGHTTP2_ERR_CALLBACK_FAILURE;
                }
            }
            else if (strncmp((const char *)name, ":scheme", namelen) == 0)
                ; /* ignore scheme */
            else if (strncmp((const char *)name, ":authority", namelen) == 0)
            {
                /* authority not currently used by routing */
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
    if (!source || !source->ptr)
    {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }
    StreamData *data = (StreamData *)source->ptr;
    if (!data || !data->resp)
    {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }
    size_t remaining = data->resp->body_len - data->resp_sent;
    size_t to_copy = remaining < length ? remaining : length;
    if (to_copy > 0)
    {
        memcpy(buf, data->resp->body + data->resp_sent, to_copy);
        data->resp_sent += to_copy;
    }
    if (data->resp_sent >= data->resp->body_len)
    {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
    }
    return to_copy;
}

/* Callback invoked when a complete frame is received */
static int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data)
{
    H2IO *io = (H2IO *)user_data;
    ServerConfig *config = io ? io->config : NULL;
    H2_LOG("on_frame_recv_callback: start");
    if (!frame)
        return 0;
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_REQUEST &&
        (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS))
    {
        StreamData *data = nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
        if (data)
        {
            char raw_request[BUFFER_SIZE];
            snprintf(raw_request, sizeof(raw_request), "%s %s %s\r\n",
                     data->req.method ? data->req.method : "GET",
                     data->req.path ? data->req.path : "/",
                     data->req.version ? data->req.version : "HTTP/2");
            log_message(LOG_LEVEL_INFO, "Routing HTTP/2 request for path (synthesized): %s",
                        data->req.path ? data->req.path : "/");
            if (!data->resp)
                data->resp = calloc(1, sizeof(Http2Response));
            if (!data->resp)
            {
                log_message(LOG_LEVEL_ERROR, "Failed to allocate Http2Response");
                return 0;
            }
            data->resp_sent = 0;
            if (route_request_tls(&data->req, raw_request, strlen(raw_request), config, NULL, data->resp) != 0 &&
                data->resp->status_code == 0) {
                data->resp->status_code = 500;
                snprintf(data->resp->status_text, sizeof(data->resp->status_text), "Internal Server Error");
                data->resp->content_type[0] = '\0';
                data->resp->body[0] = '\0';
                data->resp->body_len = 0;
            }
            if (data->resp->status_code == 0)
                data->resp->status_code = 200;
            snprintf(data->resp->status_code_str, sizeof(data->resp->status_code_str),
                     "%d", data->resp->status_code);
            snprintf(data->resp->content_length_str, sizeof(data->resp->content_length_str),
                     "%zu", data->resp->body_len);
            data->resp->headers[0] = MAKE_NV(":status", data->resp->status_code_str);
            data->resp->headers[1] = MAKE_NV("content-type",
                                             data->resp->content_type[0] ? data->resp->content_type : "text/plain");
            data->resp->headers[2] = MAKE_NV("content-length", data->resp->content_length_str);
            data->resp->num_headers = 3;
            nghttp2_data_provider data_prd;
            data_prd.source.ptr = data;
            data_prd.read_callback = http2_body_read_callback;
            int rv = nghttp2_submit_response(session, frame->hd.stream_id,
                                             data->resp->headers, data->resp->num_headers,
                                             &data_prd);
            if (rv != 0)
                log_message(LOG_LEVEL_ERROR, "nghttp2_submit_response failed: %s", nghttp2_strerror(rv));
            else
                log_message(LOG_LEVEL_INFO, "nghttp2_submit_response succeeded for stream %d", frame->hd.stream_id);
            if (rv == 0)
            {
                int send_rv = nghttp2_session_send(session);
                if (send_rv < 0 && send_rv != NGHTTP2_ERR_WOULDBLOCK)
                    log_message(LOG_LEVEL_ERROR, "nghttp2_session_send failed: %s", nghttp2_strerror(send_rv));
            }
        }
    }
    return 0;
}

/* Callback invoked when a stream is closed */
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
        if (data->resp)
        {
            free(data->resp);
        }
        free(data);
        nghttp2_session_set_stream_user_data(session, stream_id, NULL);
    }
    return 0;
}

SSL_CTX *ssl_ctx = NULL;
static struct io_uring global_ring;
static volatile sig_atomic_t g_shutdown = 0;
static int g_server_fd = -1;
static _Atomic size_t g_in_flight_requests = 0;
static struct timespec g_shutdown_deadline;
static _Atomic int g_shutdown_deadline_set = 0;

static void log_io_uring_error(const char *context, int ret)
{
    log_message(LOG_LEVEL_ERROR, "%s: %s", context, strerror(-ret));
}

static void handle_signal(int sig)
{
    (void)sig;
    if (!atomic_exchange(&g_shutdown, 1)) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        g_shutdown_deadline.tv_sec = now.tv_sec + 30;
        g_shutdown_deadline.tv_nsec = now.tv_nsec;
        atomic_store(&g_shutdown_deadline_set, 1);
        log_message(LOG_LEVEL_INFO, "Graceful shutdown initiated (SIGTERM/SIGINT). Draining %zu in-flight requests with 30s timeout",
                    atomic_load(&g_in_flight_requests));
    }
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RD);
    }
}

static void handle_http2_connection(SSL *ssl, int client_fd, ServerConfig *config, struct io_uring *ring);
static void handle_http1_connection(SSL *ssl, int client_fd, ServerConfig *config);

/* Callback for nghttp2 to send data via SSL */
static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                             size_t length, int flags, void *user_data)
{
    (void)session;
    (void)flags;
    H2IO *io = (H2IO *)user_data;
    io->want_read = 0;
    io->want_write = 0;

    ssize_t ret = SSL_write(io->ssl, data, length);
    if (ret > 0)
    {
        H2_LOG("h2 send_callback wrote=%zd", ret);
        return ret;
    }

    int ssl_error = SSL_get_error(io->ssl, ret);
    if (ssl_error == SSL_ERROR_WANT_READ) {
        io->want_read = 1;
        H2_LOG("h2 send_callback WANT_READ");
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    if (ssl_error == SSL_ERROR_WANT_WRITE) {
        io->want_write = 1;
        H2_LOG("h2 send_callback WANT_WRITE");
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    log_message(LOG_LEVEL_ERROR, "SSL_write failed in send_callback. Error: %d", ssl_error);
    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

/* Callback for nghttp2 to receive data via SSL */
static ssize_t recv_callback(nghttp2_session *session, uint8_t *buf, size_t length,
                             int flags, void *user_data)
{
    H2_LOG("recv_callback: start");
    (void)session;
    (void)flags;
    H2IO *io = (H2IO *)user_data;
    io->want_read = 0;
    io->want_write = 0;

    ssize_t ret = SSL_read(io->ssl, buf, length);
    if (ret <= 0)
    {
        int ssl_error = SSL_get_error(io->ssl, ret);
        if (ssl_error == SSL_ERROR_ZERO_RETURN)
        {
            log_message(LOG_LEVEL_INFO, "SSL connection closed by peer");
            return NGHTTP2_ERR_EOF;
        }
        if (ssl_error == SSL_ERROR_WANT_READ) {
            io->want_read = 1;
            H2_LOG("h2 recv_callback WANT_READ");
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        if (ssl_error == SSL_ERROR_WANT_WRITE) {
            io->want_write = 1;
            H2_LOG("h2 recv_callback WANT_WRITE");
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        log_message(LOG_LEVEL_ERROR, "SSL_read failed in recv_callback. Error: %d", ssl_error);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    io->total_read += (size_t)ret;
    H2_LOG("recv_callback: end, bytes read: %zd", ret);
    return ret;
}

/* Nonblocking TLS handshake using thread-local io_uring with instrumentation */
static int perform_nonblocking_ssl_accept(SSL *ssl, int client_fd, struct io_uring *ring)
{
    int ret;
    struct timeval start, end;
    gettimeofday(&start, NULL);
    while ((ret = SSL_accept(ssl)) <= 0)
    {
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        {
            int poll_flags = (err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            if (!sqe)
            {
                log_message(LOG_LEVEL_ERROR, "Failed to get SQE during handshake");
                return -1;
            }
            io_uring_prep_poll_add(sqe, client_fd, poll_flags);
            int submit_ret = io_uring_submit(ring);
            if (submit_ret < 0)
            {
                log_io_uring_error("io_uring_submit failed during handshake", submit_ret);
                return -1;
            }
            struct io_uring_cqe *cqe;
            int wait_ret = io_uring_wait_cqe(ring, &cqe);
            if (wait_ret < 0)
            {
                log_io_uring_error("io_uring_wait_cqe failed during handshake", wait_ret);
                return -1;
            }
            io_uring_cqe_seen(ring, cqe);
        }
        else
        {
            log_message(LOG_LEVEL_ERROR, "SSL_accept failed nonblockingly, error: %d", err);
            return -1;
        }
    }
    gettimeofday(&end, NULL);
    long handshake_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
    log_message(LOG_LEVEL_INFO, "Nonblocking SSL handshake completed in %ld ms", handshake_ms);
    return ret;
}

/* HTTP/2 connection handler using thread-local io_uring */
static void handle_http2_connection(SSL *ssl, int client_fd, ServerConfig *config, struct io_uring *ring)
{
    (void)ring;
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
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
    H2IO io = {
        .ssl = ssl,
        .config = config,
        .want_read = 0,
        .want_write = 0,
        .total_read = 0,
    };
    if (nghttp2_session_server_new(&session, callbacks, &io) != 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to create nghttp2 session");
        nghttp2_session_callbacks_del(callbacks);
        return;
    }
    log_message(LOG_LEVEL_INFO, "HTTP/2 session started");
    int rv = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0);
    if (rv < 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to send initial SETTINGS frame: %s", nghttp2_strerror(rv));
        nghttp2_session_del(session);
        nghttp2_session_callbacks_del(callbacks);
        return;
    }
    rv = nghttp2_session_send(session);
    if (rv < 0 && rv != NGHTTP2_ERR_WOULDBLOCK)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to flush initial SETTINGS: %s", nghttp2_strerror(rv));
        nghttp2_session_del(session);
        nghttp2_session_callbacks_del(callbacks);
        return;
    }
    rv = 0;
    while (nghttp2_session_want_read(session) || nghttp2_session_want_write(session))
    {
        short events = 0;
        if (io.want_read || io.want_write)
        {
            if (io.want_read)
                events |= POLLIN;
            if (io.want_write)
                events |= POLLOUT;
        }
        else
        {
            if (nghttp2_session_want_read(session))
                events |= POLLIN;
            if (nghttp2_session_want_write(session))
                events |= POLLOUT;
        }

        H2_LOG("h2 loop: want_read=%d want_write=%d io.want_read=%d io.want_write=%d events=0x%x",
               nghttp2_session_want_read(session), nghttp2_session_want_write(session),
               io.want_read, io.want_write, events);
        struct pollfd pfd = {.fd = client_fd, .events = events};
        int pret = poll(&pfd, 1, H2_POLL_TIMEOUT_MS);
        if (pret < 0)
        {
            log_message(LOG_LEVEL_ERROR, "poll failed: %s", strerror(errno));
            break;
        }
        if (pret == 0)
            continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            log_message(LOG_LEVEL_ERROR, "poll error/hangup: revents=%d", pfd.revents);
            break;
        }
        H2_LOG("h2 loop: revents=0x%x", pfd.revents);

        if (pfd.revents & POLLIN)
        {
            rv = nghttp2_session_recv(session);
            if (rv < 0)
            {
                if (rv == NGHTTP2_ERR_EOF)
                    log_message(LOG_LEVEL_INFO, "HTTP/2 session closed by client");
                else if (rv != NGHTTP2_ERR_WOULDBLOCK)
                    log_message(LOG_LEVEL_ERROR, "nghttp2_session_recv error: %s", nghttp2_strerror(rv));
                if (rv != NGHTTP2_ERR_WOULDBLOCK)
                    break;
            }
            else
            {
                H2_LOG("h2 recv processed rv=%d total_read=%zu", rv, io.total_read);
            }
        }
        if (pfd.revents & POLLOUT)
        {
            rv = nghttp2_session_send(session);
            if (rv < 0)
            {
                if (rv != NGHTTP2_ERR_WOULDBLOCK)
                {
                    log_message(LOG_LEVEL_ERROR, "nghttp2_session_send error: %s", nghttp2_strerror(rv));
                    break;
                }
            }
            else
            {
                H2_LOG("h2 send processed rv=%d", rv);
            }
        }
    }
    nghttp2_session_del(session);
    nghttp2_session_callbacks_del(callbacks);
}

/* HTTP/1.1 connection handler using legacy methods */
static void handle_http1_connection(SSL *ssl, int client_fd, ServerConfig *config)
{
    (void)client_fd;

    // Serve multiple HTTP/1.x requests on the same TLS socket
    for (;;) {
        char buffer[BUFFER_SIZE];
        int total_read = 0;

        // 1) Read up to the end of headers (\r\n\r\n)
        int header_end = -1;
        while (total_read < BUFFER_SIZE - 1) {
            int n = SSL_read(ssl, buffer + total_read,
                             BUFFER_SIZE - 1 - total_read);
            if (n <= 0) {
                // client closed connection or SSL error
                goto shutdown_and_close;
            }
            total_read += n;
            header_end = find_header_end(buffer, (size_t)total_read);
            if (header_end >= 0)
                break;
        }
        buffer[total_read] = '\0';
        if (header_end < 0) {
            const char *too_large =
                "HTTP/1.1 431 Request Header Fields Too Large\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
            SSL_write(ssl, too_large, strlen(too_large));
            goto shutdown_and_close;
        }

        // 2) Parse the request
        HttpRequest req;
        if (parse_http_request(buffer, total_read, &req) != 0) {
            const char *bad_response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
            SSL_write(ssl, bad_response, strlen(bad_response));
            // malformed request → close connection
            goto shutdown_and_close;
        }

        log_message(LOG_LEVEL_INFO, "Valid HTTP request received. Routing...");

        // 3) Route & send response (static, proxy, etc.)
        route_request_tls(&req, buffer, total_read, config, ssl, NULL);

        // 4) Loop back to read the next request
        //    (do NOT shutdown/close here)
    }

shutdown_and_close:
    SSL_shutdown(ssl);
    close(client_fd);
}

/* Worker task: uses thread-local io_uring for per-connection I/O */
void client_task(void *arg)
{
    ClientTaskData *data = (ClientTaskData *)arg;
    static __thread struct io_uring *local_ring = NULL;
    if (!local_ring)
    {
        local_ring = malloc(sizeof(struct io_uring));
        if (!local_ring || io_uring_queue_init(QUEUE_DEPTH, local_ring, 0) < 0)
        {
            log_message(LOG_LEVEL_ERROR, "Failed to initialize thread-local io_uring");
            if (local_ring)
            {
                free(local_ring);
                local_ring = NULL;
            }
            close(data->client_fd);
            free(data);
            atomic_fetch_sub(&g_in_flight_requests, 1);
            return;
        }
    }
    handle_client(data->client_fd, data->config, local_ring);
    io_uring_queue_exit(local_ring);
    free(local_ring);
    local_ring = NULL;
    free(data);
    atomic_fetch_sub(&g_in_flight_requests, 1);
}

/* Main per-connection handler; performs nonblocking TLS handshake before dispatching via ALPN */
void handle_client(int client_fd, ServerConfig *config, struct io_uring *ring)
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
    if (perform_nonblocking_ssl_accept(ssl, client_fd, ring) <= 0)
    {
        log_message(LOG_LEVEL_ERROR, "Nonblocking SSL handshake failed");
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
        handle_http2_connection(ssl, client_fd, config, ring);
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

/* Main server accept loop using a global io_uring instance */
int start_server(ServerConfig *config)
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int ring_ret;
    size_t max_threads;
    size_t initial_threads;

    g_shutdown = 0;
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    ring_ret = io_uring_queue_init(QUEUE_DEPTH * 2, &global_ring, 0);
    if (ring_ret != 0)
    {
        fprintf(stderr, "global io_uring_queue_init failed: %s\n", strerror(-ring_ret));
        return -1;
    }
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("Socket creation error");
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    int enable = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    g_server_fd = server_fd;
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Bind error");
        close(server_fd);
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    if (listen(server_fd, 2048) == -1)
    {
        perror("Listen error");
        close(server_fd);
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    ssl_ctx = create_ssl_context(config->ssl.certificate, config->ssl.private_key, config);
    if (!ssl_ctx)
    {
        close(server_fd);
        io_uring_queue_exit(&global_ring);
        return -1;
    }

    max_threads = config->max_connections > 0 ? (size_t)config->max_connections : 32;
    initial_threads = max_threads < 32 ? max_threads : 32;

    ThreadPool *pool = thread_pool_create(initial_threads, max_threads);
    if (!pool)
    {
        fprintf(stderr, "Failed to create thread pool\n");
        close(server_fd);
        io_uring_queue_exit(&global_ring);
        cleanup_ssl_context(ssl_ctx);
        return -1;
    }
    printf("Emme listening on port %d...\n", config->port);
    
    time_t last_stats_log = time(NULL);
    while (!g_shutdown)
    {
        time_t now = time(NULL);
        if (now - last_stats_log > 60)
        {
            log_session_stats(ssl_ctx);
            last_stats_log = now;
        }
        
        struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
        socklen_t client_len = sizeof(client_addr);

        if (!sqe)
        {
            fprintf(stderr, "Failed to get SQE for accept\n");
            break;
        }
        io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)&client_addr, &client_len, 0);
        int submit_ret = io_uring_submit(&global_ring);
        if (submit_ret < 0)
        {
            fprintf(stderr, "io_uring_submit (accept) failed: %s\n", strerror(-submit_ret));
            break;
        }
        struct io_uring_cqe *cqe;
        int wait_ret = io_uring_wait_cqe(&global_ring, &cqe);
        if (wait_ret < 0)
        {
            if (g_shutdown &&
                (-wait_ret == EINTR || -wait_ret == EBADF || -wait_ret == ENXIO))
                break;
            fprintf(stderr, "io_uring_wait_cqe (accept) failed: %s\n", strerror(-wait_ret));
            break;
        }
        client_fd = cqe->res;
        io_uring_cqe_seen(&global_ring, cqe);
        if (g_shutdown)
        {
            if (client_fd >= 0)
                close(client_fd);
            break;
        }
        if (client_fd < 0)
        {
            if (g_shutdown)
                break;
            continue;
        }
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);
        ClientTaskData *task_data = malloc(sizeof(ClientTaskData));
        if (!task_data)
        {
            perror("Failed to allocate memory for client task");
            close(client_fd);
            continue;
        }
        task_data->client_fd = client_fd;
        task_data->config = config;
        atomic_fetch_add(&g_in_flight_requests, 1);
        if (!thread_pool_add_task(pool, client_task, task_data))
        {
            fprintf(stderr, "Failed to add task to thread pool\n");
            atomic_fetch_sub(&g_in_flight_requests, 1);
            free(task_data);
            close(client_fd);
        }
    }
    thread_pool_destroy(pool);
    close(server_fd);
    g_server_fd = -1;
    
    if (atomic_load(&g_shutdown_deadline_set)) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        
        while (atomic_load(&g_in_flight_requests) > 0) {
            if (now.tv_sec > g_shutdown_deadline.tv_sec ||
                (now.tv_sec == g_shutdown_deadline.tv_sec && 
                 now.tv_nsec > g_shutdown_deadline.tv_nsec)) {
                log_message(LOG_LEVEL_WARN, "Graceful shutdown timeout reached. Forcing shutdown with %zu in-flight requests",
                            atomic_load(&g_in_flight_requests));
                break;
            }
            
            log_message(LOG_LEVEL_INFO, "Waiting for %zu in-flight requests to complete...",
                        atomic_load(&g_in_flight_requests));
            struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = 100000000};
            nanosleep(&sleep_time, NULL);
            clock_gettime(CLOCK_REALTIME, &now);
        }
        
        if (atomic_load(&g_in_flight_requests) == 0) {
            log_message(LOG_LEVEL_INFO, "Graceful shutdown complete. All requests drained.");
        }
    }
    
    io_uring_queue_exit(&global_ring);
    cleanup_ssl_context(ssl_ctx);
    return 0;
}
