// tests/integration/test_server.c

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

#include "server.h"

#define PORT 8443
#define CONFIG_YAML "tests/integration/test_config.yml"
#define STATIC_DIR "temp_static"

// ----------------------------------------
// Suite setup/teardown
// ----------------------------------------

static pid_t server_pid = -1;

// write a minimal YAML config pointing at static and your certs/dev.*
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

// spawn your `emme` binary in the background
static void launch_server(void)
{
    server_pid = fork();
    cr_assert(server_pid >= 0, "fork failed");

    if (server_pid == 0)
    {
        // child → exec the server process
        execlp("./emme", "emme", "--config", CONFIG_YAML, NULL);
        // if exec fails:
        perror("execlp");
        _exit(1);
    }

    // parent → give it a moment to bind/listen
    usleep(300 * 1000);
}

// kill the server and wait
static void teardown_server(void)
{
    if (server_pid > 0)
    {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
    }
    // cleanup
    unlink(CONFIG_YAML);
    unlink(STATIC_DIR "/index.html");
    rmdir(STATIC_DIR);
}

// This runs once before any tests in this file
TestSuite(https_blackbox, .timeout = 15,
          .init = write_test_config,
          .fini = teardown_server);

// ----------------------------------------
// OpenSSL client helpers
// ----------------------------------------

static SSL_CTX *make_client_ctx(void)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *meth = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(meth);
    cr_assert_not_null(ctx, "SSL_CTX_new");

    // trust the self‑signed cert
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

static int ssl_write_all(SSL *ssl, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        int n = SSL_write(ssl, buf + off, len - off);
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

static int read_headers_only(SSL *ssl, char *buf, size_t buflen)
{
    int total = 0;
    while (total < (int)buflen - 1)
    {
        int n = SSL_read(ssl, buf + total, buflen - 1 - total);
        if (n <= 0)
            break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
    }
    return total;
}

// ----------------------------------------
// Test case
// ----------------------------------------

Test(https_blackbox, get_static_and_404)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    // GET existing file → 200 OK + body
    const char *req1 =
        "GET /static/index.html HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    cr_assert_gt(SSL_write(ssl, req1, strlen(req1)), 0, "write1");
    char buf[4096];

    size_t total = 0;
    int content_length = -1;
    int header_end = -1;

    // Read until headers and body are complete
    while (total < sizeof(buf) - 1) {
        int n = SSL_read(ssl, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        // Find end of headers
        if (header_end < 0) {
            char *p = strstr(buf, "\r\n\r\n");
            if (p) {
                header_end = p - buf + 4;
                // Find Content-Length
                char *cl = strstr(buf, "Content-Length:");
                if (cl) {
                    sscanf(cl, "Content-Length: %d", &content_length);
                }
            }
        }
        // If headers found and body received, break
        if (header_end > 0 && content_length >= 0 && total >= (size_t)(header_end + content_length))
            break;
    }

    cr_assert_gt(total, 0, "read1");
    buf[total] = '\0';
    cr_assert(strstr(buf, "HTTP/1.1 200 OK"),
              "Expected 200 OK, got:\n%s", buf);
    cr_assert(strstr(buf, "Hello, world!"),
              "Expected body, got:\n%s", buf);

    // GET missing file → 404 Not Found
    const char *req2 =
        "GET /static/notfound.html HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    cr_assert_gt(SSL_write(ssl, req2, strlen(req2)), 0, "write2");

    memset(buf, 0, sizeof(buf));
    int total2 = 0;
    int header_end2 = -1;
    while (total2 < (int)(sizeof(buf) - 1)) {
       int n2 = SSL_read(ssl, buf + total2, sizeof(buf) - 1 - total2);
       if (n2 <= 0) {
            // If we already have some data, connection close is fine
            if (total2 > 0) break;
            // Otherwise, fail
            cr_assert_gt(n2, 0, "read2");
        }
        total2 += n2;
        buf[total2] = '\0';
        if (header_end2 < 0) {
            char *p2 = strstr(buf, "\r\n\r\n");
            if (p2) {
                header_end2 = p2 - buf + 4;
                break; // 404 has no body, so we can stop here
            }
        }
    }


    cr_assert_gt(total2, 0, "read2");
    buf[total2] = '\0';
    cr_assert(strstr(buf, "HTTP/1.1 404 Not Found"),
              "Expected 404 Not Found, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

Test(https_blackbox, bad_request_400)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    const char *req = "GET /static/index.html\r\n\r\n";
    cr_assert_eq(ssl_write_all(ssl, req, strlen(req)), 0, "write");

    char buf[4096];
    int n = read_headers_only(ssl, buf, sizeof(buf));
    cr_assert_gt(n, 0, "read");
    cr_assert(strstr(buf, "HTTP/1.1 400 Bad Request"),
              "Expected 400, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

Test(https_blackbox, headers_too_large_431)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    char big[BUFFER_SIZE];
    memset(big, 'A', sizeof(big));

    cr_assert_eq(ssl_write_all(ssl, big, sizeof(big)), 0, "write");

    char buf[4096];
    int n = read_headers_only(ssl, buf, sizeof(buf));
    cr_assert_gt(n, 0, "read");
    cr_assert(strstr(buf, "HTTP/1.1 431 Request Header Fields Too Large"),
              "Expected 431, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

Test(https_blackbox, health_endpoint_healthy)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    const char *req =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    cr_assert_eq(ssl_write_all(ssl, req, strlen(req)), 0, "write");

    char buf[4096];
    int total = 0;
    int header_end = -1;
    int content_length = 15;

    while (total < (int)(sizeof(buf) - 1)) {
        int n = SSL_read(ssl, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        if (header_end < 0) {
            char *p = strstr(buf, "\r\n\r\n");
            if (p) {
                header_end = p - buf + 4;
            }
        }

        if (header_end > 0 && total >= header_end + content_length)
            break;
    }

    cr_assert_gt(total, 0, "read");
    buf[total] = '\0';
    cr_assert(strstr(buf, "HTTP/1.1 200 OK"),
              "Expected 200 OK, got:\n%s", buf);
    cr_assert(strstr(buf, "{\"status\":\"ok\"}"),
              "Expected JSON body, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

Test(https_blackbox, health_endpoint_json_format)
{
    launch_server();

    SSL_CTX *ctx = make_client_ctx();
    SSL *ssl = connect_ssl(ctx);

    const char *req =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    cr_assert_eq(ssl_write_all(ssl, req, strlen(req)), 0, "write");

    char buf[4096];
    int total = 0;
    int content_length = -1;
    int header_end = -1;

    while (total < (int)(sizeof(buf) - 1)) {
        int n = SSL_read(ssl, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        if (header_end < 0) {
            char *p = strstr(buf, "\r\n\r\n");
            if (p) {
                header_end = p - buf + 4;
                char *cl = strstr(buf, "Content-Length:");
                if (cl) {
                    sscanf(cl, "Content-Length: %d", &content_length);
                }
            }
        }

        if (header_end > 0 && content_length >= 0 && 
            total >= header_end + content_length)
            break;
    }

    cr_assert_gt(total, 0, "read");
    buf[total] = '\0';
    
    char *json_start = strstr(buf, "{");
    cr_assert_not_null(json_start, "Expected JSON body");
    
    char *body = json_start;
    cr_assert(strstr(body, "\"status\":\"ok\""), "Expected status:ok");
    
    cr_assert(strstr(buf, "Content-Type: application/json"),
              "Expected JSON content type, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}
