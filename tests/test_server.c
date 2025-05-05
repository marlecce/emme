#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <server.h>
#include <config.h>
#include <http_parser.h>
#include <thread_pool.h>

typedef struct {
    int fd;
    ServerConfig *config;
} ServerThreadArg;

void *server_thread_func(void *arg) {
    ServerThreadArg *thread_arg = (ServerThreadArg *)arg;
    handle_client(thread_arg->fd, thread_arg->config);
    return NULL;
}

int main() {
    // Create a temporary directory and static file
    const char *static_dir = "temp_static";
    mkdir(static_dir, 0755);
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/index.html", static_dir);
    FILE *f = fopen(filepath, "w");
    assert(f != NULL);
    fprintf(f, "Hello, world!");
    fclose(f);

    // Create a temporary configuration to serve static files
    ServerConfig config;
    memset(&config, 0, sizeof(ServerConfig));
    config.port = 8080;
    config.max_connections = 10;
    strcpy(config.log_level, "INFO");
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, static_dir);
    // Set SSL configuration: use the development self-signed certificate
    strcpy(config.ssl.certificate, "certs/dev.crt");
    strcpy(config.ssl.private_key, "certs/dev.key");

    // Create a socket pair to simulate client-server TLS communication
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    // Initialize SSL for the client side
    SSL_library_init();
    SSL_load_error_strings();
    const SSL_METHOD *client_method = TLS_client_method();
    SSL_CTX *client_ctx = SSL_CTX_new(client_method);
    assert(client_ctx != NULL);

    // Create an SSL object for the client side using sv[0]
    SSL *client_ssl = SSL_new(client_ctx);
    SSL_set_fd(client_ssl, sv[0]);

    // Create a thread to simulate the server side on sv[1]
    ServerThreadArg arg;
    arg.fd = sv[1];
    arg.config = &config;
    pthread_t server_thread;
    int ret = pthread_create(&server_thread, NULL, server_thread_func, &arg);
    assert(ret == 0);

    // Client: perform the TLS handshake
    if (SSL_connect(client_ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        assert(0 && "SSL_connect failed");
    }

    // Test: GET /static/index.html
    const char *request = "GET /static/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    SSL_write(client_ssl, request, strlen(request));

    char buffer[BUFFER_SIZE];
    int n = SSL_read(client_ssl, buffer, BUFFER_SIZE - 1);
    assert(n > 0);
    buffer[n] = '\0';

    // Check for "HTTP/1.1 200 OK" and "Hello, world!" in response
    assert(strstr(buffer, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(buffer, "Hello, world!") != NULL);
    printf("Test handle_client passed!\n");

    // Test: GET /static/notfound.html (expect 404)
    const char *request_404 = "GET /static/notfound.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    SSL_write(client_ssl, request_404, strlen(request_404));
    n = SSL_read(client_ssl, buffer, BUFFER_SIZE - 1);
    assert(n > 0);
    buffer[n] = '\0';

    // Check for "HTTP/1.1 404 Not Found" in response
    assert(strstr(buffer, "HTTP/1.1 404 Not Found") != NULL);
    printf("Test for non-existing resource (404) passed!\n");

    // Cleanup
    SSL_shutdown(client_ssl);
    SSL_free(client_ssl);
    close(sv[0]);

    pthread_join(server_thread, NULL);
    close(sv[1]);

    SSL_CTX_free(client_ctx);

    remove(filepath);
    rmdir(static_dir);

    return 0;
}
