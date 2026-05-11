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
#include "metrics.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "thread_pool.h"
#include "log.h"
#include <ctype.h>
#include "http2_response.h"
#include "uuid.h"

#ifndef DEBUG_H2
#define DEBUG_H2 0
#endif

#define H2_LOG(...)                         \
    do {                                    \
        if (DEBUG_H2)                       \
            log_message(LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } while (0)

#define H2_POLL_TIMEOUT_MS 100
#define H2_POLL_ERROR_EVENTS (POLLERR | POLLHUP | POLLNVAL)

#define SERVER_BACKLOG 2048
#define THREAD_POOL_MIN_THREADS 32
#define THREAD_POOL_MAX_THREADS_RATIO 1
#define SESSION_STATS_INTERVAL_SEC 60

#define NS_PER_MS 1000000
#define US_PER_MS 1000

typedef struct {
    SSL *ssl;
    ServerConfig *config;
    int want_read;
    int want_write;
    size_t total_read;
    int request_count;
    struct timeval request_start;
    int request_timeout_ms;
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
    (void)flags;
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_REQUEST)
    {
        H2IO *io = (H2IO *)user_data;
        if (io && frame->headers.cat == NGHTTP2_HCAT_REQUEST &&
            namelen == 7 && strncmp((const char *)name, ":method", 7) == 0)
        {
            io->request_count++;
            gettimeofday(&io->request_start, NULL);
        }
        
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
            generate_uuid(data->req.request_id);
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
static int g_server_fd = -1;

shutdown_context_t g_shutdown_ctx = {0};

static void log_io_uring_error(const char *context, int ret)
{
    log_message(LOG_LEVEL_ERROR, "%s: %s", context, strerror(-ret));
}

static void client_task(void *arg);
void handle_signal(int sig);

static void cleanup_server_resources(ThreadPool *pool, int server_fd)
{
    if (pool) {
        thread_pool_destroy(pool);
    }
    if (server_fd >= 0) {
        close(server_fd);
        g_server_fd = -1;
    }
    io_uring_queue_exit(&global_ring);
    if (ssl_ctx) {
        cleanup_ssl_context(ssl_ctx);
        ssl_ctx = NULL;
    }
}

static int initialize_server(ServerConfig *config, ThreadPool **out_pool)
{
    int ring_ret;
    
    if (!config || !out_pool) {
        log_message(LOG_LEVEL_ERROR, "Invalid parameters to initialize_server");
        return -1;
    }
    
    memset(&g_shutdown_ctx, 0, sizeof(g_shutdown_ctx));
    g_shutdown_ctx.timeout_seconds = config->shutdown_timeout_seconds;
    atomic_store(&g_shutdown_ctx.state, SHUTDOWN_STATE_RUNNING);
    
    ring_ret = io_uring_queue_init(QUEUE_DEPTH * 2, &global_ring, 0);
    if (ring_ret != 0) {
        log_io_uring_error("io_uring_queue_init", ring_ret);
        return -1;
    }
    
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd == -1) {
        log_message(LOG_LEVEL_ERROR, "Socket creation failed: %s", strerror(errno));
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    
    int enable = 1;
    if (setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        log_message(LOG_LEVEL_ERROR, "setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);
    
    if (bind(g_server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        log_message(LOG_LEVEL_ERROR, "Bind error on port %d: %s", config->port, strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    
    if (listen(g_server_fd, SERVER_BACKLOG) == -1) {
        log_message(LOG_LEVEL_ERROR, "Listen error: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    
    ssl_ctx = create_ssl_context(config->ssl.certificate, config->ssl.private_key, config);
    if (!ssl_ctx) {
        log_message(LOG_LEVEL_ERROR, "Failed to create SSL context");
        close(g_server_fd);
        g_server_fd = -1;
        io_uring_queue_exit(&global_ring);
        return -1;
    }
    
    size_t max_threads = config->max_connections > 0 ? (size_t)config->max_connections : 32;
    size_t initial_threads = max_threads < THREAD_POOL_MIN_THREADS ? max_threads : THREAD_POOL_MIN_THREADS;
    
    *out_pool = thread_pool_create(initial_threads, max_threads);
    if (!*out_pool) {
        log_message(LOG_LEVEL_ERROR, "Failed to create thread pool");
        close(g_server_fd);
        g_server_fd = -1;
        io_uring_queue_exit(&global_ring);
        cleanup_ssl_context(ssl_ctx);
        ssl_ctx = NULL;
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "Server initialized on port %d (max_connections=%zu)", 
                config->port, max_threads);
    return 0;
}

static int accept_and_dispatch_client(ThreadPool *pool, ServerConfig *config)
{
    struct io_uring_sqe *sqe;
    struct sockaddr_in client_addr;
    struct io_uring_cqe *cqe;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    int submit_ret, wait_ret;
    shutdown_state_t state;
    int flags;
    ClientTaskData *task_data;
    
    sqe = io_uring_get_sqe(&global_ring);
    if (!sqe) {
        log_message(LOG_LEVEL_ERROR, "Failed to get SQE for accept");
        return -1;
    }
    
    io_uring_prep_accept(sqe, g_server_fd, (struct sockaddr *)&client_addr, &client_len, 0);
    
    submit_ret = io_uring_submit(&global_ring);
    if (submit_ret < 0) {
        log_io_uring_error("io_uring_submit (accept)", submit_ret);
        return -1;
    }
    
    wait_ret = io_uring_wait_cqe(&global_ring, &cqe);
    if (wait_ret < 0) {
        state = atomic_load(&g_shutdown_ctx.state);
        if (state != SHUTDOWN_STATE_RUNNING &&
            (-wait_ret == EINTR || -wait_ret == EBADF || -wait_ret == ENXIO)) {
            return 1;
        }
        log_io_uring_error("io_uring_wait_cqe (accept)", wait_ret);
        return -1;
    }
    
    client_fd = cqe->res;
    io_uring_cqe_seen(&global_ring, cqe);
    
    state = atomic_load(&g_shutdown_ctx.state);
    if (state != SHUTDOWN_STATE_RUNNING) {
        if (client_fd >= 0) {
            close(client_fd);
        }
        return 1;
    }
    
    if (client_fd < 0) {
        return (state != SHUTDOWN_STATE_RUNNING) ? 1 : 0;
    }
    
    flags = fcntl(client_fd, F_GETFL, 0);
    if (flags == -1) {
        log_message(LOG_LEVEL_ERROR, "fcntl(F_GETFL) failed: %s", strerror(errno));
        close(client_fd);
        return 0;
    }
    
    if (fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        log_message(LOG_LEVEL_ERROR, "fcntl(F_SETFL) failed: %s", strerror(errno));
        close(client_fd);
        return 0;
    }
    
    task_data = malloc(sizeof(ClientTaskData));
    if (!task_data) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate client task data: %s", strerror(errno));
        close(client_fd);
        return 0;
    }
    
    task_data->client_fd = client_fd;
    task_data->config = config;
    
    atomic_fetch_add(&g_shutdown_ctx.in_flight_requests, 1);
    metrics_set_active_connections(atomic_load(&g_shutdown_ctx.in_flight_requests));
    
    if (!thread_pool_add_task(pool, client_task, task_data)) {
        log_message(LOG_LEVEL_ERROR, "Failed to add task to thread pool");
        atomic_fetch_sub(&g_shutdown_ctx.in_flight_requests, 1);
        metrics_set_active_connections(atomic_load(&g_shutdown_ctx.in_flight_requests));
        free(task_data);
        close(client_fd);
    }
    
    return 0;
}

static void drain_in_flight_requests(void)
{
    struct timespec now;
    time_t last_log_time = 0;
    size_t remaining, peak;
    long drain_duration_ms;
    
    clock_gettime(CLOCK_REALTIME, &now);
    
    while (atomic_load(&g_shutdown_ctx.in_flight_requests) > 0) {
        clock_gettime(CLOCK_REALTIME, &now);
        
        if (now.tv_sec > g_shutdown_ctx.deadline.tv_sec ||
            (now.tv_sec == g_shutdown_ctx.deadline.tv_sec && 
             now.tv_nsec > g_shutdown_ctx.deadline.tv_nsec)) {
            
            atomic_store(&g_shutdown_ctx.state, SHUTDOWN_STATE_FORCED);
            
            remaining = atomic_load(&g_shutdown_ctx.in_flight_requests);
            log_message(LOG_LEVEL_WARN, 
                        "Graceful shutdown timeout (%ds) reached. Forcing shutdown with %zu in-flight requests",
                        g_shutdown_ctx.timeout_seconds, remaining);
            break;
        }
        
        if (now.tv_sec != last_log_time) {
            log_message(LOG_LEVEL_INFO, 
                        "Draining: %zu requests still in-flight...",
                        atomic_load(&g_shutdown_ctx.in_flight_requests));
            last_log_time = now.tv_sec;
        }
        
        struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = 100 * NS_PER_MS};
        nanosleep(&sleep_time, NULL);
    }
    
    clock_gettime(CLOCK_REALTIME, &g_shutdown_ctx.metrics.end_time);
    
    peak = atomic_load(&g_shutdown_ctx.metrics.peak_in_flight);
    remaining = atomic_load(&g_shutdown_ctx.in_flight_requests);
    atomic_store(&g_shutdown_ctx.metrics.completed, peak - remaining);
    atomic_store(&g_shutdown_ctx.metrics.forced, remaining);
    
    drain_duration_ms = 
        (g_shutdown_ctx.metrics.end_time.tv_sec - g_shutdown_ctx.metrics.start_time.tv_sec) * 1000 +
        (g_shutdown_ctx.metrics.end_time.tv_nsec - g_shutdown_ctx.metrics.start_time.tv_nsec) / NS_PER_MS;
    
    log_message(LOG_LEVEL_INFO, 
                "Graceful shutdown complete. "
                "Duration: %ldms | Completed: %zu | Forced: %zu | Peak: %zu",
                drain_duration_ms,
                atomic_load(&g_shutdown_ctx.metrics.completed),
                atomic_load(&g_shutdown_ctx.metrics.forced),
                atomic_load(&g_shutdown_ctx.metrics.peak_in_flight));
}

static void perform_shutdown(ThreadPool *pool, int server_fd)
{
    shutdown_state_t state = atomic_load(&g_shutdown_ctx.state);
    
    if (state == SHUTDOWN_STATE_DRAINING) {
        drain_in_flight_requests();
    } else if (state == SHUTDOWN_STATE_FORCED) {
        log_message(LOG_LEVEL_INFO, "Immediate shutdown completed (SIGINT)");
    }
    
    cleanup_server_resources(pool, server_fd);
    
    log_session_stats(ssl_ctx);
}

void handle_signal(int sig)
{
    shutdown_state_t old_state = atomic_load(&g_shutdown_ctx.state);
    if (old_state != SHUTDOWN_STATE_RUNNING) {
        return;
    }
    
    if (sig == SIGINT) {
        atomic_store(&g_shutdown_ctx.state, SHUTDOWN_STATE_FORCED);
        log_message(LOG_LEVEL_WARN, "SIGINT received - immediate shutdown (development mode)");
    } else {
        atomic_store(&g_shutdown_ctx.state, SHUTDOWN_STATE_DRAINING);
        
        clock_gettime(CLOCK_REALTIME, &g_shutdown_ctx.deadline);
        g_shutdown_ctx.deadline.tv_sec += g_shutdown_ctx.timeout_seconds;
        
        atomic_store(&g_shutdown_ctx.metrics.peak_in_flight,
                     atomic_load(&g_shutdown_ctx.in_flight_requests));
        clock_gettime(CLOCK_REALTIME, &g_shutdown_ctx.metrics.start_time);
        
        log_message(LOG_LEVEL_INFO, 
                    "SIGTERM received - graceful shutdown initiated. "
                    "Draining %zu in-flight requests with %ds timeout",
                    atomic_load(&g_shutdown_ctx.in_flight_requests),
                    g_shutdown_ctx.timeout_seconds);
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
    
    ServerConfig *config = SSL_get_app_data(ssl);
    int timeout_ms = config ? config->tls_handshake_timeout_ms : 10000;
    
    while ((ret = SSL_accept(ssl)) <= 0)
    {
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        {
            gettimeofday(&end, NULL);
            long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                              (end.tv_usec - start.tv_usec) / US_PER_MS;
            if (elapsed_ms > timeout_ms) {
                log_message(LOG_LEVEL_WARN, "TLS handshake timeout: %ldms exceeded (limit %dms)",
                            elapsed_ms, timeout_ms);
                return -1;
            }
            
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
    long handshake_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / US_PER_MS;
    log_message(LOG_LEVEL_INFO, "Nonblocking SSL handshake completed in %ld ms", handshake_ms);
    metrics_increment_tls_handshake(1);
    metrics_record_tls_handshake_duration(handshake_ms / 1000.0);
    return ret;
}

static nghttp2_session *h2_session_init(nghttp2_session_callbacks **out_callbacks,
                                         H2IO *io, ServerConfig *config)
{
    nghttp2_session *session = NULL;
    nghttp2_option *options = NULL;
    
    if (nghttp2_session_callbacks_new(out_callbacks) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to initialize HTTP/2 callbacks");
        return NULL;
    }
    
    nghttp2_session_callbacks_set_send_callback(*out_callbacks, send_callback);
    nghttp2_session_callbacks_set_recv_callback(*out_callbacks, recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(*out_callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(*out_callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(*out_callbacks, on_stream_close_callback);
    
    if (nghttp2_option_new(&options) == 0) {
        nghttp2_option_set_peer_max_concurrent_streams(options,
            (uint32_t)config->http2.max_concurrent_streams);
    }
    
    if (nghttp2_session_server_new2(&session, *out_callbacks, io, options) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create nghttp2 session");
        nghttp2_session_callbacks_del(*out_callbacks);
        *out_callbacks = NULL;
        if (options) nghttp2_option_del(options);
        return NULL;
    }
    
    if (options) nghttp2_option_del(options);
    
    log_message(LOG_LEVEL_INFO, "HTTP/2 session started (max_streams=%d keepalive=%ds)",
                config->http2.max_concurrent_streams, config->http2.keepalive_timeout);
    
    return session;
}

static int h2_session_send_initial_settings(nghttp2_session *session)
{
    int rv = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0);
    if (rv < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to send initial SETTINGS frame: %s", nghttp2_strerror(rv));
        return -1;
    }
    
    rv = nghttp2_session_send(session);
    if (rv < 0 && rv != NGHTTP2_ERR_WOULDBLOCK) {
        log_message(LOG_LEVEL_ERROR, "Failed to flush initial SETTINGS: %s", nghttp2_strerror(rv));
        return -1;
    }
    
    return 0;
}

/* HTTP/2 connection handler using thread-local io_uring */
static void handle_http2_connection(SSL *ssl, int client_fd, ServerConfig *config, struct io_uring *ring)
{
    (void)ring;
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    nghttp2_session_callbacks *callbacks = NULL;
    H2IO io = {
        .ssl = ssl,
        .config = config,
        .want_read = 0,
        .want_write = 0,
        .total_read = 0,
        .request_count = 0,
        .request_timeout_ms = config->request_timeout_ms,
    };
    
    nghttp2_session *session = h2_session_init(&callbacks, &io, config);
    if (!session) {
        return;
    }
    
    if (h2_session_send_initial_settings(session) != 0) {
        nghttp2_session_del(session);
        nghttp2_session_callbacks_del(callbacks);
        return;
    }
    
    time_t last_activity = time(NULL);
    time_t connection_start = last_activity;
    int rv = 0;
    
    while (nghttp2_session_want_read(session) || nghttp2_session_want_write(session))
    {
        time_t now = time(NULL);
        
        if (now - last_activity > config->http2.keepalive_timeout) {
            log_message(LOG_LEVEL_INFO, "HTTP/2 connection timeout: idle %lds (max %ds)",
                        (long)(now - last_activity), config->http2.keepalive_timeout);
            break;
        }
        
        if (io.request_count >= config->http2.max_requests_per_connection) {
            log_message(LOG_LEVEL_INFO, "HTTP/2 connection closed: reached max requests (%d)",
                        config->http2.max_requests_per_connection);
            break;
        }
        
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        int elapsed_ms = (int)((tv_now.tv_sec - io.request_start.tv_sec) * 1000 +
                               (tv_now.tv_usec - io.request_start.tv_usec) / 1000);
        if (io.request_count > 0 && elapsed_ms > io.request_timeout_ms) {
            log_message(LOG_LEVEL_WARN, "HTTP/2 request timeout: %dms exceeded (limit %dms)",
                        elapsed_ms, io.request_timeout_ms);
            metrics_increment_request_timeouts();
            break;
        }
        
        short events = 0;
        if (io.want_read || io.want_write) {
            if (io.want_read) events |= POLLIN;
            if (io.want_write) events |= POLLOUT;
        } else {
            if (nghttp2_session_want_read(session)) events |= POLLIN;
            if (nghttp2_session_want_write(session)) events |= POLLOUT;
        }

        H2_LOG("h2 loop: want_read=%d want_write=%d io.want_read=%d io.want_write=%d events=0x%x",
               nghttp2_session_want_read(session), nghttp2_session_want_write(session),
               io.want_read, io.want_write, events);
        
        struct pollfd pfd = {.fd = client_fd, .events = events};
        int pret = poll(&pfd, 1, H2_POLL_TIMEOUT_MS);
        if (pret < 0) {
            log_message(LOG_LEVEL_ERROR, "poll failed: %s", strerror(errno));
            break;
        }
        if (pret == 0) continue;
        
        if (pfd.revents & H2_POLL_ERROR_EVENTS) {
            log_message(LOG_LEVEL_ERROR, "poll error/hangup: revents=%d", pfd.revents);
            break;
        }
        
        H2_LOG("h2 loop: revents=0x%x", pfd.revents);

        if (pfd.revents & POLLIN) {
            rv = nghttp2_session_recv(session);
            if (rv < 0) {
                if (rv == NGHTTP2_ERR_EOF) {
                    log_message(LOG_LEVEL_INFO, "HTTP/2 session closed by client");
                } else if (rv != NGHTTP2_ERR_WOULDBLOCK) {
                    log_message(LOG_LEVEL_ERROR, "nghttp2_session_recv error: %s", nghttp2_strerror(rv));
                }
                if (rv != NGHTTP2_ERR_WOULDBLOCK) break;
            } else {
                H2_LOG("h2 recv processed rv=%d total_read=%zu", rv, io.total_read);
                last_activity = now;
            }
        }
        
        if (pfd.revents & POLLOUT) {
            rv = nghttp2_session_send(session);
            if (rv < 0 && rv != NGHTTP2_ERR_WOULDBLOCK) {
                log_message(LOG_LEVEL_ERROR, "nghttp2_session_send error: %s", nghttp2_strerror(rv));
                break;
            } else if (rv >= 0) {
                H2_LOG("h2 send processed rv=%d", rv);
                last_activity = now;
            }
        }
    }
    
    time_t conn_duration = time(NULL) - connection_start;
    log_message(LOG_LEVEL_INFO, "HTTP/2 session ended: duration=%lds requests=%d",
                (long)conn_duration, io.request_count);
    
    nghttp2_session_del(session);
    nghttp2_session_callbacks_del(callbacks);
}

/* HTTP/1.1 connection handler - synchronous SSL I/O with keep-alive */
static void handle_http1_connection(SSL *ssl, int client_fd, ServerConfig *config)
{
    (void)client_fd;
    struct timeval request_start;
    int timeout_ms = config->request_timeout_ms;

    // Serve multiple HTTP/1.x requests on the same TLS socket
    for (;;) {
        char buffer[BUFFER_SIZE];
        int total_read = 0;
        gettimeofday(&request_start, NULL);

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
            
            struct timeval now;
            gettimeofday(&now, NULL);
            int elapsed_ms = (int)((now.tv_sec - request_start.tv_sec) * 1000 +
                                   (now.tv_usec - request_start.tv_usec) / 1000);
            if (elapsed_ms > timeout_ms) {
                log_message(LOG_LEVEL_WARN, "Request timeout: %dms exceeded (limit %dms)",
                            elapsed_ms, timeout_ms);
                metrics_increment_request_timeouts();
                const char *timeout_response =
                    "HTTP/1.1 408 Request Timeout\r\n"
                    "Content-Length: 0\r\n"
                    "Retry-After: 5\r\n"
                    "\r\n";
                SSL_write(ssl, timeout_response, strlen(timeout_response));
                goto shutdown_and_close;
            }
            
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

        log_message(LOG_LEVEL_INFO, "Valid HTTP request received [id=%s]. Routing...", req.request_id);

        // 3) Route & send response (static, proxy, etc.)
        route_request_tls(&req, buffer, total_read, config, ssl, NULL);
        
        struct timeval request_end;
        gettimeofday(&request_end, NULL);
        double request_duration = (request_end.tv_sec - request_start.tv_sec) + 
                                  (request_end.tv_usec - request_start.tv_usec) / (double)US_PER_MS;
        metrics_increment_request(req.method, req.path, 200);
        metrics_record_request_duration(request_duration);

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
            atomic_fetch_sub(&g_shutdown_ctx.in_flight_requests, 1);
            metrics_set_active_connections(atomic_load(&g_shutdown_ctx.in_flight_requests));
            return;
        }
    }
    handle_client(data->client_fd, data->config, local_ring);
    io_uring_queue_exit(local_ring);
    free(local_ring);
    local_ring = NULL;
    free(data);
    atomic_fetch_sub(&g_shutdown_ctx.in_flight_requests, 1);
    metrics_set_active_connections(atomic_load(&g_shutdown_ctx.in_flight_requests));
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
        metrics_increment_tls_handshake(0);
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
    ThreadPool *pool = NULL;
    time_t last_stats_log = time(NULL);
    int accept_result;
    
    if (!config) {
        log_message(LOG_LEVEL_ERROR, "start_server: config parameter is NULL");
        return -1;
    }
    
    if (initialize_server(config, &pool) != 0) {
        log_message(LOG_LEVEL_ERROR, "Server initialization failed");
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "Emme listening on port %d...", config->port);
    
    while (atomic_load(&g_shutdown_ctx.state) == SHUTDOWN_STATE_RUNNING) {
        time_t now = time(NULL);
        
        if (now - last_stats_log >= SESSION_STATS_INTERVAL_SEC) {
            log_session_stats(ssl_ctx);
            last_stats_log = now;
        }
        
        accept_result = accept_and_dispatch_client(pool, config);
        
        if (accept_result == 1) {
            break;
        } else if (accept_result < 0) {
            log_message(LOG_LEVEL_ERROR, "Accept loop error, shutting down");
            break;
        }
    }
    
    perform_shutdown(pool, g_server_fd);
    
    return 0;
}
