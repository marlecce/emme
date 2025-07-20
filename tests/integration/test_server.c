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

#define PORT 8443
#define CONFIG_YAML "tests/integration/test_config.yml"
#define STATIC_DIR "temp_static"

// ----------------------------------------
// Suite setup/teardown
// ----------------------------------------

static pid_t server_pid = -1;

// write a minimal YAML config pointing at temp_static and your certs/dev.*
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
            "port: %d\n"
            "max_connections: 5\n"
            "log_level: ERROR\n"
            "routes:\n"
            "  - path: /static/\n"
            "    technology: static\n"
            "    document_root: %s\n"
            "ssl:\n"
            "  certificate: certs/dev.crt\n"
            "  private_key:  certs/dev.key\n",
            PORT, STATIC_DIR);
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
    int n = SSL_read(ssl, buf, sizeof(buf) - 1);
    cr_assert_gt(n, 0, "read1");
    buf[n] = '\0';
    cr_assert(strstr(buf, "HTTP/1.1 200 OK"),
              "Expected 200 OK, got:\n%s", buf);
    cr_assert(strstr(buf, "Hello, world!"),
              "Expected body, got:\n%s", buf);

    // GET missing file → 404 Not Found
    const char *req2 =
        "GET /static/notfound.html HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    cr_assert_gt(SSL_write(ssl, req2, strlen(req2)), 0, "write2");
    n = SSL_read(ssl, buf, sizeof(buf) - 1);
    cr_assert_gt(n, 0, "read2");
    buf[n] = '\0';
    cr_assert(strstr(buf, "HTTP/1.1 404 Not Found"),
              "Expected 404 Not Found, got:\n%s", buf);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}
