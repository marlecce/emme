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

#define BUFFER_SIZE 1024
#define QUEUE_DEPTH 64

SSL_CTX *ssl_ctx = NULL;

void client_task(void *arg) {
    ClientTaskData *data = (ClientTaskData *)arg;
    handle_client(data->client_fd, data->config);
    free(data);  // Free the allocated memory after processing.
}

void handle_client(int client_fd, ServerConfig *config) {
    SSL *ssl = SSL_new(ssl_ctx);
    if (!ssl) {
        close(client_fd);
        return;
    }
    SSL_set_fd(ssl, client_fd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(client_fd);
        return;
    }

    char buffer[BUFFER_SIZE];
    int n = SSL_read(ssl, buffer, BUFFER_SIZE - 1);
    if (n <= 0) {
        SSL_free(ssl);
        close(client_fd);
        return;
    }
    buffer[n] = '\0';

    HttpRequest req;
    if (parse_http_request(buffer, n, &req) != 0) {
        const char *bad_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        SSL_write(ssl, bad_response, strlen(bad_response));
        SSL_free(ssl);
        close(client_fd);
        return;
    }

    route_request_tls(&req, buffer, n, config, ssl);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_fd);
}

int start_server(ServerConfig *config) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct io_uring ring;
    
    // Create and initialize io_uring for accepting connections.
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) != 0) {
        perror("io_uring_queue_init failed");
        return 1;
    }

    // Create the server socket.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation error");
        return 1;
    }

    // Bind the socket to the address and port.
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind error");
        close(server_fd);
        return 1;
    }

    // Start listening for incoming connections.
    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("Listen error");
        close(server_fd);
        return 1;
    }

    // Initialize SSL context.
    ssl_ctx = create_ssl_context(config->ssl.certificate, config->ssl.private_key);

    // Create the dynamic thread pool.
    ThreadPool *pool = thread_pool_create(4, config->max_connections);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        close(server_fd);
        return 1;
    }
    printf("Emme listening on port %d...\n", config->port);

    // Main loop using io_uring to accept new connections
    while (1) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            fprintf(stderr, "Failed to get submission queue entry\n");
            break;
        }

        io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)&client_addr, &client_len, 0);
        if (io_uring_submit(&ring) < 0) {
            perror("io_uring_submit failed");
            break;
        }

        struct io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&ring, &cqe) < 0) {
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
        if (!task_data) {
            perror("Failed to allocate memory for client task");
            close(client_fd);
            continue;
        }
        task_data->client_fd = client_fd;
        task_data->config = config;

         // Add the client handling task to the thread pool.
         if (!thread_pool_add_task(pool, client_task, task_data)) {
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
