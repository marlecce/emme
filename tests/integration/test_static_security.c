// tests/integration/test_static_security.c

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

#include <openssl/ssl.h>
#include <openssl/err.h>

#define PORT 8445
#define CONFIG_YAML "tests/integration/test_security_config.yml"
#define STATIC_DIR "temp_static_sec"

static pid_t server_pid = -1;

static void write_test_config(void)
{
    mkdir(STATIC_DIR, 0755);
    FILE *fhtml = fopen(STATIC_DIR "/index.html", "w");
    cr_assert_not_null(fhtml, "fopen index.html");
    fprintf(fhtml, "Hello, secure world!");
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
        "    document_root: \"%s\"\n",
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

TestSuite(https_security, .timeout = 15,
          .init = write_test_config,
          .fini = teardown_server);

static SSL_CTX *make_client_ctx(void)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *meth = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    cr_assert_not_null(ctx, "SSL_CTX_new");

    cr_assert_eq(SSL_CTX_load_verify_locations(ctx,
                                               "certs/dev.crt", NULL),
                 1, "SSL_CTX_load_verify_locations");
    return ctx;
}

static SSL *connect_ssl(SSL_CTX *ctx)
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
    return ssl;
}

static int read_response(SSL *ssl, char *buf, size_t buflen)
{
    size_t total = 0;
    while (total < buflen - 1)
    {
        int n = SSL_read(ssl, buf + total, buflen - 1 - total);
        if (n <= 0)
            break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
    }
    return (int)total;
}

Test(https_security, blocks_path_traversal)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    const char *req =
        "GET /static/../index.html HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    cr_assert_gt(SSL_write(ssl, req, strlen(req)), 0, "write");

    char buf[4096];
    int n = read_response(ssl, buf, sizeof(buf));
    cr_assert_gt(n, 0, "read");
    cr_assert(strstr(buf, "HTTP/1.1 403 Forbidden") || strstr(buf, "HTTP/1.1 404 Not Found"),
              "Expected 403 or 404, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

Test(https_security, rejects_large_headers)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    char buf[9000];
    size_t off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < 300; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "X-%d: value\r\n", i);
        if (off >= sizeof(buf) - 32)
            break;
    }
    off += snprintf(buf + off, sizeof(buf) - off, "\r\n");
    cr_assert_gt(SSL_write(ssl, buf, off), 0, "write");

    char resp[4096];
    int n = read_response(ssl, resp, sizeof(resp));
    cr_assert_gt(n, 0, "read");
    cr_assert(strstr(resp, "HTTP/1.1 400 Bad Request"),
              "Expected 400 Bad Request, got:\n%s", resp);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}
