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
#include "server.h"
#include "config.h"

#define MAX_EVENTS 100
#define BUFFER_SIZE 1024
#define QUEUE_DEPTH 64

// Struttura per la gestione dei worker thread
typedef struct
{
    int client_fd;
} WorkerTask;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
WorkerTask task_queue[MAX_EVENTS];
int queue_head = 0, queue_tail = 0;

// Funzione che gestisce una singola connessione client
void handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];
    // Legge la richiesta (qui non viene fatto un parsing avanzato)
    read(client_fd, buffer, BUFFER_SIZE);

    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, world!";

    send(client_fd, response, strlen(response), 0);
    close(client_fd);
}

// Funzione utilizzata dal thread pool per elaborare le richieste
void *worker_function(void *arg)
{
    (void)arg; // Ignora il parametro non usato
    while (1)
    {
        pthread_mutex_lock(&queue_mutex);
        while (queue_head == queue_tail)
        {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        WorkerTask task = task_queue[queue_tail];
        queue_tail = (queue_tail + 1) % MAX_EVENTS;
        pthread_mutex_unlock(&queue_mutex);

        // Chiamata alla funzione handle_client per gestire la richiesta
        handle_client(task.client_fd);
    }
    return NULL;
}

int start_server(ServerConfig *config)
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct io_uring ring;
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("Errore creazione socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Errore bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == -1)
    {
        perror("Errore listen");
        close(server_fd);
        return 1;
    }

    // Creazione del thread pool per gestire i client
    pthread_t workers[config->max_connections];
    for (int i = 0; i < config->max_connections; i++)
    {
        pthread_create(&workers[i], NULL, worker_function, NULL);
    }

    printf("Server in ascolto sulla porta %d...\n", config->port);

    // Loop principale con io_uring per accettare nuove connessioni
    while (1)
    {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)&client_addr, &client_len, 0);
        io_uring_submit(&ring);

        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        client_fd = cqe->res;
        io_uring_cqe_seen(&ring, cqe);

        pthread_mutex_lock(&queue_mutex);
        task_queue[queue_head].client_fd = client_fd;
        queue_head = (queue_head + 1) % MAX_EVENTS;
        pthread_cond_signal(&queue_cond);
        pthread_mutex_unlock(&queue_mutex);
    }

    close(server_fd);
    io_uring_queue_exit(&ring);
    return 0;
}
