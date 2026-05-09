// tests/unit/test_router.c

#include <criterion/criterion.h>

#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "router.h"

#define STATIC_DIR "temp_static_router"
#define SECRET_FILE "router_secret.txt"

static void init_openssl(void)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

static SSL_CTX *create_server_ctx(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    cr_assert_not_null(ctx, "SSL_CTX_new");
    cr_assert_eq(SSL_CTX_use_certificate_file(ctx, "certs/dev.crt", SSL_FILETYPE_PEM), 1,
                 "SSL_CTX_use_certificate_file");
    cr_assert_eq(SSL_CTX_use_PrivateKey_file(ctx, "certs/dev.key", SSL_FILETYPE_PEM), 1,
                 "SSL_CTX_use_PrivateKey_file");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

static SSL_CTX *create_client_ctx(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    cr_assert_not_null(ctx, "SSL_CTX_new");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

static void handshake_pair(SSL *client, SSL *server)
{
    int client_done = 0;
    int server_done = 0;
    for (int i = 0; i < 1000 && (!client_done || !server_done); i++) {
        if (!client_done) {
            int r = SSL_do_handshake(client);
            if (r == 1) {
                client_done = 1;
            } else {
                int err = SSL_get_error(client, r);
                cr_assert(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE,
                          "client handshake error: %d", err);
            }
        }
        if (!server_done) {
            int r = SSL_do_handshake(server);
            if (r == 1) {
                server_done = 1;
            } else {
                int err = SSL_get_error(server, r);
                cr_assert(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE,
                          "server handshake error: %d", err);
            }
        }
    }
    cr_assert(client_done && server_done, "handshake timeout");
}

static void create_ssl_pair(SSL **out_server, SSL **out_client)
{
    init_openssl();
    SSL_CTX *server_ctx = create_server_ctx();
    SSL_CTX *client_ctx = create_client_ctx();

    SSL *server = SSL_new(server_ctx);
    SSL *client = SSL_new(client_ctx);
    cr_assert_not_null(server, "SSL_new server");
    cr_assert_not_null(client, "SSL_new client");

    BIO *client_bio = NULL;
    BIO *server_bio = NULL;
    cr_assert_eq(BIO_new_bio_pair(&client_bio, 0, &server_bio, 0), 1, "BIO_new_bio_pair");

    SSL_set_bio(client, client_bio, client_bio);
    SSL_set_bio(server, server_bio, server_bio);

    SSL_set_connect_state(client);
    SSL_set_accept_state(server);

    handshake_pair(client, server);

    *out_server = server;
    *out_client = client;

    SSL_CTX_free(server_ctx);
    SSL_CTX_free(client_ctx);
}

static int read_ssl_response(SSL *ssl, char *buf, size_t buflen)
{
    size_t total = 0;
    int header_end = -1;
    int content_length = -1;
    for (int i = 0; i < 200 && total < buflen - 1; i++) {
        int n = SSL_read(ssl, buf + total, buflen - 1 - total);
        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                continue;
            break;
        }
        total += (size_t)n;
        buf[total] = '\0';
        if (header_end < 0) {
            char *p = strstr(buf, "\r\n\r\n");
            if (p) {
                header_end = (int)(p - buf + 4);
                char *cl = strstr(buf, "Content-Length:");
                if (cl)
                    sscanf(cl, "Content-Length: %d", &content_length);
            }
        }
        if (header_end > 0 && content_length >= 0 &&
            total >= (size_t)(header_end + content_length))
            break;
    }
    return (int)total;
}

static void setup_static_dir(void)
{
    mkdir(STATIC_DIR, 0755);
    FILE *f = fopen(STATIC_DIR "/index.html", "w");
    cr_assert_not_null(f, "fopen index.html");
    fprintf(f, "Hello router");
    fclose(f);
}

static void cleanup_static_dir(void)
{
    unlink(STATIC_DIR "/index.html");
    rmdir(STATIC_DIR);
}

Test(router_static, serves_file_over_tls)
{
    setup_static_dir();

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/index.html";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0, "no response");
    cr_assert(strstr(resp, "HTTP/1.1 200 OK"), "Expected 200 OK, got:\n%s", resp);
    cr_assert(strstr(resp, "Hello router"), "Expected body, got:\n%s", resp);

    SSL_free(server);
    SSL_free(client);
    cleanup_static_dir();
}

Test(router_static, blocks_traversal_outside_root)
{
    setup_static_dir();

    FILE *secret = fopen(SECRET_FILE, "w");
    cr_assert_not_null(secret, "fopen secret");
    fprintf(secret, "secret");
    fclose(secret);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/../" SECRET_FILE;

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0, "no response");
    cr_assert(strstr(resp, "HTTP/1.1 403 Forbidden"), "Expected 403, got:\n%s", resp);

    SSL_free(server);
    SSL_free(client);
    unlink(SECRET_FILE);
    cleanup_static_dir();
}

Test(router_h2, route_sets_body_and_type)
{
    HttpRequest req = {0};
    req.path = "/";
    Http2Response resp = {0};

    int rc = route_request_tls(&req, "GET / HTTP/2\r\n", 14, NULL, NULL, &resp);
    cr_assert_eq(rc, 0);
    cr_assert_gt(resp.body_len, 0);
    cr_assert_str_eq(resp.content_type, "text/html");
    cr_assert_eq(resp.status_code, 200);
}

Test(router_static, rejects_empty_document_root)
{
    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    config.routes[0].document_root[0] = '\0';

    HttpRequest req = {0};
    req.path = "/static/index.html";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), -1);

    SSL_free(server);
    SSL_free(client);
}

Test(router_static, serves_css_file)
{
    setup_static_dir();
    FILE *f = fopen(STATIC_DIR "/style.css", "w");
    cr_assert_not_null(f);
    fprintf(f, "body { color: red; }");
    fclose(f);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/style.css";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0);
    cr_assert(strstr(resp, "HTTP/1.1 200 OK"));
    cr_assert(strstr(resp, "text/css"));
    cr_assert(strstr(resp, "color: red"));

    SSL_free(server);
    SSL_free(client);
    unlink(STATIC_DIR "/style.css");
    cleanup_static_dir();
}

Test(router_static, serves_json_file)
{
    setup_static_dir();
    FILE *f = fopen(STATIC_DIR "/data.json", "w");
    cr_assert_not_null(f);
    fprintf(f, "{\"key\":\"value\"}");
    fclose(f);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/data.json";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0);
    cr_assert(strstr(resp, "application/json"));
    cr_assert(strstr(resp, "{\"key\":\"value\"}"));

    SSL_free(server);
    SSL_free(client);
    unlink(STATIC_DIR "/data.json");
    cleanup_static_dir();
}

Test(router_static, serves_png_file)
{
    setup_static_dir();
    FILE *f = fopen(STATIC_DIR "/image.png", "wb");
    cr_assert_not_null(f);
    fprintf(f, "\x89PNG\r\n\x1a\n");
    fclose(f);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/image.png";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0);
    cr_assert(strstr(resp, "image/png"));

    SSL_free(server);
    SSL_free(client);
    unlink(STATIC_DIR "/image.png");
    cleanup_static_dir();
}

Test(router_static, handles_null_request)
{
    ServerConfig config = {0};
    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(NULL, &config, server), -1);

    SSL_free(server);
    SSL_free(client);
}

Test(router_static, handles_null_path)
{
    ServerConfig config = {0};
    HttpRequest req = {0};
    req.path = NULL;
    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), -1);

    SSL_free(server);
    SSL_free(client);
}

Test(router_static, handles_no_matching_route)
{
    setup_static_dir();

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/other/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/index.html";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), -1);

    SSL_free(server);
    SSL_free(client);
    cleanup_static_dir();
}

Test(router_static, serves_jpg_file)
{
    setup_static_dir();
    FILE *f = fopen(STATIC_DIR "/photo.jpg", "wb");
    cr_assert_not_null(f);
    fprintf(f, "\xFF\xD8\xFF");
    fclose(f);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/photo.jpg";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0);
    cr_assert(strstr(resp, "image/jpeg"));

    SSL_free(server);
    SSL_free(client);
    unlink(STATIC_DIR "/photo.jpg");
    cleanup_static_dir();
}

Test(router_static, serves_js_file)
{
    setup_static_dir();
    FILE *f = fopen(STATIC_DIR "/script.js", "w");
    cr_assert_not_null(f);
    fprintf(f, "console.log('test');");
    fclose(f);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/script.js";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0);
    cr_assert(strstr(resp, "application/javascript"));
    cr_assert(strstr(resp, "console.log"));

    SSL_free(server);
    SSL_free(client);
    unlink(STATIC_DIR "/script.js");
    cleanup_static_dir();
}

Test(router_static, serves_txt_file)
{
    setup_static_dir();
    FILE *f = fopen(STATIC_DIR "/readme.txt", "w");
    cr_assert_not_null(f);
    fprintf(f, "Hello text");
    fclose(f);

    ServerConfig config = {0};
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, STATIC_DIR);

    HttpRequest req = {0};
    req.path = "/static/readme.txt";

    SSL *server = NULL;
    SSL *client = NULL;
    create_ssl_pair(&server, &client);

    cr_assert_eq(serve_static_tls(&req, &config, server), 0);

    char resp[4096];
    int n = read_ssl_response(client, resp, sizeof(resp));
    cr_assert_gt(n, 0);
    cr_assert(strstr(resp, "text/plain"));
    cr_assert(strstr(resp, "Hello text"));

    SSL_free(server);
    SSL_free(client);
    unlink(STATIC_DIR "/readme.txt");
    cleanup_static_dir();
}
