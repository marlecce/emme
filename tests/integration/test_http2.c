// tests/integration/test_http2.c

#include <criterion/criterion.h>
#include <criterion/logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <nghttp2/nghttp2.h>

#ifndef DEBUG_H2
#define DEBUG_H2 0
#endif

#define H2C_LOG(...)                    \
    do {                                \
        if (DEBUG_H2)                   \
            fprintf(stderr, __VA_ARGS__); \
    } while (0)

#define PORT 8444
#define CONFIG_YAML "tests/integration/test_http2_config.yml"
#define STATIC_DIR "temp_static_h2"

static pid_t server_pid = -1;

static void write_test_config(void)
{
    mkdir(STATIC_DIR, 0755);
    FILE *fhtml = fopen(STATIC_DIR "/index.html", "w");
    cr_assert_not_null(fhtml, "fopen index.html");
    fprintf(fhtml, "Hello, world!");
    fclose(fhtml);

    mkdir("tests/integration", 0755);
    FILE *f = fopen(CONFIG_YAML, "w");
    cr_assert_not_null(f, "fopen config.yml");
    fprintf(f,
        "server:\n"
        "  port: %d\n"
        "  max_connections: 100\n"
        "  log_level: ERROR\n"
        "\n"
        "logging:\n"
        "  file: emme.log\n"
        "  level: debug\n"
        "  format: plain\n"
        "  buffer_size: 16384\n"
        "  rollover_size: 10485760\n"
        "  rollover_daily: true\n"
        "  appender_flags:\n"
        "    - file\n"
        "    - console\n"
        "\n"
        "ssl:\n"
        "  certificate: \"certs/dev.crt\"\n"
        "  private_key:  \"certs/dev.key\"\n"
        "\n"
        "routes:\n"
        "  - path: \"/static/\"\n"
        "    technology: \"static\"\n"
        "    document_root: \"%s\"\n"
        "  - path: \"/api/\"\n"
        "    technology: \"reverse_proxy\"\n"
        "    backend: \"127.0.0.1:8081\"\n",
        PORT, STATIC_DIR
    );
    fclose(f);
}

static void launch_server(void)
{
    server_pid = fork();
    cr_assert(server_pid >= 0, "fork failed");

    if (server_pid == 0)
    {
        execlp("./emme", "emme", "--config", CONFIG_YAML, NULL);
        perror("execlp");
        _exit(1);
    }

    usleep(300 * 1000);
}

static void teardown_server(void)
{
    if (server_pid > 0)
    {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
    }
    unlink(CONFIG_YAML);
    unlink(STATIC_DIR "/index.html");
    rmdir(STATIC_DIR);
}

TestSuite(https2_blackbox, .timeout = 15,
          .init = write_test_config,
          .fini = teardown_server);

static SSL_CTX *make_client_ctx_h2(void)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *meth = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    cr_assert_not_null(ctx, "SSL_CTX_new");

    const unsigned char protos[] = "\x02h2";
    cr_assert_eq(SSL_CTX_set_alpn_protos(ctx, protos, sizeof(protos) - 1), 0,
                 "SSL_CTX_set_alpn_protos");

    cr_assert_eq(SSL_CTX_load_verify_locations(ctx,
                                               "certs/dev.crt", NULL),
                 1, "SSL_CTX_load_verify_locations");
    return ctx;
}

static SSL *connect_ssl_h2(SSL_CTX *ctx)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    cr_assert(sock >= 0, "socket()");

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}};

    cr_assert_eq(connect(sock, (struct sockaddr *)&sa, sizeof(sa)), 0,
                 "connect() failed");

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    cr_assert_gt(SSL_connect(ssl), 0, "SSL_connect failed");

    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    cr_assert(alpn_len == 2 && memcmp(alpn, "h2", 2) == 0, "ALPN h2 not negotiated");

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    return ssl;
}

typedef struct {
    int stream_id;
    int done;
    int status;
    char body[4096];
    size_t body_len;
} ClientState;

typedef struct {
    SSL *ssl;
    int want_read;
    int want_write;
    size_t total_written;
    size_t total_read;
    ClientState state;
} ClientCtx;

static ssize_t client_send_callback(nghttp2_session *session, const uint8_t *data,
                                    size_t length, int flags, void *user_data)
{
    (void)session;
    (void)flags;
    ClientCtx *ctx = (ClientCtx *)user_data;
    ssize_t ret = SSL_write(ctx->ssl, data, length);
    if (ret <= 0)
    {
        int ssl_error = SSL_get_error(ctx->ssl, ret);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
        {
            ctx->want_read = (ssl_error == SSL_ERROR_WANT_READ);
            ctx->want_write = (ssl_error == SSL_ERROR_WANT_WRITE);
            H2C_LOG("[h2 client] send WANT_%s\n",
                    ssl_error == SSL_ERROR_WANT_READ ? "READ" : "WRITE");
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        H2C_LOG("[h2 client] send error=%d\n", ssl_error);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    ctx->want_read = 0;
    ctx->want_write = 0;
    ctx->total_written += (size_t)ret;
    H2C_LOG("[h2 client] send wrote=%zd total=%zu\n", ret, ctx->total_written);
    return ret;
}

static ssize_t client_recv_callback(nghttp2_session *session, uint8_t *buf,
                                    size_t length, int flags, void *user_data)
{
    (void)session;
    (void)flags;
    ClientCtx *ctx = (ClientCtx *)user_data;
    ssize_t ret = SSL_read(ctx->ssl, buf, length);
    if (ret <= 0)
    {
        int ssl_error = SSL_get_error(ctx->ssl, ret);
        if (ssl_error == SSL_ERROR_ZERO_RETURN)
            return NGHTTP2_ERR_EOF;
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
        {
            ctx->want_read = (ssl_error == SSL_ERROR_WANT_READ);
            ctx->want_write = (ssl_error == SSL_ERROR_WANT_WRITE);
            H2C_LOG("[h2 client] recv WANT_%s\n",
                    ssl_error == SSL_ERROR_WANT_READ ? "READ" : "WRITE");
            return NGHTTP2_ERR_WOULDBLOCK;
        }
        H2C_LOG("[h2 client] recv error=%d\n", ssl_error);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    ctx->want_read = 0;
    ctx->want_write = 0;
    ctx->total_read += (size_t)ret;
    H2C_LOG("[h2 client] recv read=%zd total=%zu\n", ret, ctx->total_read);
    return ret;
}

static int client_on_header_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     const uint8_t *name, size_t namelen,
                                     const uint8_t *value, size_t valuelen,
                                     uint8_t flags, void *user_data)
{
    (void)session;
    (void)flags;
    ClientCtx *ctx = (ClientCtx *)user_data;
    ClientState *state = &ctx->state;
    if (frame->hd.type == NGHTTP2_HEADERS &&
        frame->headers.cat == NGHTTP2_HCAT_RESPONSE &&
        namelen == 7 && memcmp(name, ":status", 7) == 0)
    {
        char status_buf[4] = {0};
        size_t copy_len = valuelen < sizeof(status_buf) - 1 ? valuelen : sizeof(status_buf) - 1;
        memcpy(status_buf, value, copy_len);
        state->status = atoi(status_buf);
    }
    return 0;
}

static int client_on_data_chunk_recv_callback(nghttp2_session *session,
                                              uint8_t flags, int32_t stream_id,
                                              const uint8_t *data, size_t len,
                                              void *user_data)
{
    (void)session;
    (void)flags;
    (void)stream_id;
    ClientCtx *ctx = (ClientCtx *)user_data;
    ClientState *state = &ctx->state;
    size_t space = sizeof(state->body) - 1 - state->body_len;
    size_t to_copy = len < space ? len : space;
    if (to_copy > 0)
    {
        memcpy(state->body + state->body_len, data, to_copy);
        state->body_len += to_copy;
        state->body[state->body_len] = '\0';
    }
    return 0;
}

static int client_on_stream_close_callback(nghttp2_session *session,
                                           int32_t stream_id, uint32_t error_code,
                                           void *user_data)
{
    (void)session;
    (void)stream_id;
    (void)error_code;
    ClientCtx *ctx = (ClientCtx *)user_data;
    ctx->state.done = 1;
    return 0;
}

static void run_h2_get_request(const char *path, int expected_status, const char *expected_body_substr)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx_h2();
    SSL *ssl = connect_ssl_h2(ctx);

    nghttp2_session_callbacks *callbacks = NULL;
    nghttp2_session *session = NULL;
    cr_assert_eq(nghttp2_session_callbacks_new(&callbacks), 0, "callbacks_new");
    nghttp2_session_callbacks_set_send_callback(callbacks, client_send_callback);
    nghttp2_session_callbacks_set_recv_callback(callbacks, client_recv_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, client_on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, client_on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, client_on_stream_close_callback);

    ClientCtx ctx_state = {0};
    ctx_state.ssl = ssl;
    cr_assert_eq(nghttp2_session_client_new(&session, callbacks, &ctx_state), 0, "client_new");

    cr_assert_eq(nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0), 0, "submit_settings");

    nghttp2_nv hdrs[] = {
        {(uint8_t *)":method", (uint8_t *)"GET", 7, 3, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)":authority", (uint8_t *)"localhost", 10, 9, NGHTTP2_NV_FLAG_NONE},
    };

    ctx_state.state.stream_id = nghttp2_submit_request(session, NULL, hdrs, 4, NULL, NULL);
    cr_assert(ctx_state.state.stream_id > 0, "submit_request failed");

    // Send initial client preface/settings.
    nghttp2_session_send(session);

    const int timeout_ms = 10000;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!ctx_state.state.done)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L +
                          (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed_ms >= timeout_ms)
            break;

        short events = 0;
        if (ctx_state.want_read || ctx_state.want_write)
        {
            if (ctx_state.want_read)
                events |= POLLIN;
            if (ctx_state.want_write)
                events |= POLLOUT;
        }
        else
        {
            if (nghttp2_session_want_read(session))
                events |= POLLIN;
            if (nghttp2_session_want_write(session))
                events |= POLLOUT;
        }
        struct pollfd pfd;
        pfd.fd = SSL_get_fd(ssl);
        pfd.events = events;
        poll(&pfd, 1, 100);
        if (pfd.revents & POLLOUT)
            (void)nghttp2_session_send(session);
        if (pfd.revents & POLLIN)
            (void)nghttp2_session_recv(session);
    }
    cr_assert(ctx_state.state.done, "HTTP/2 response timed out");

    cr_assert_eq(ctx_state.state.status, expected_status,
                 "Expected %d, got %d", expected_status, ctx_state.state.status);
    if (expected_body_substr) {
        cr_assert(strstr(ctx_state.state.body, expected_body_substr) != NULL,
                  "Expected body to contain '%s', got:\n%s",
                  expected_body_substr, ctx_state.state.body);
    }

    nghttp2_session_del(session);
    nghttp2_session_callbacks_del(callbacks);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

Test(https2_blackbox, get_root_over_h2)
{
    run_h2_get_request("/", 200, "Welcome to High Performance Web Server");
}

Test(https2_blackbox, get_static_file_over_h2)
{
    run_h2_get_request("/static/index.html", 200, "Hello, world!");
}

Test(https2_blackbox, get_missing_static_over_h2)
{
    run_h2_get_request("/static/notfound.html", 404, NULL);
}
