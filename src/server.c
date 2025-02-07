#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "server.h"  // Aggiungi l'header che contiene le dichiarazioni delle funzioni
#include "config.h"  // Aggiungi l'header per ServerConfig

#define MAX_EVENTS 100
#define BUFFER_SIZE 1024

// Funzione che gestisce la connessione con un client
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    read(client_fd, buffer, BUFFER_SIZE); // Legge la richiesta HTTP (non parsing avanzato ancora)

    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, world!";

    write(client_fd, response, strlen(response)); // Invia la risposta
    close(client_fd);
}

// Funzione che avvia il server
int start_server(ServerConfig *config) {
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct epoll_event event, events[MAX_EVENTS];

    // 1️⃣ Creazione della socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Errore creazione socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);  // Usa la porta dalla configurazione

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Errore bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("Errore listen");
        close(server_fd);
        return 1;
    }

    // 2️⃣ Setup epoll per gestione eventi
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Errore creazione epoll");
        close(server_fd);
        return 1;
    }

    event.events = EPOLLIN;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    printf("Server in ascolto sulla porta %d...\n", config->port);

    while (1) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {
            perror("Errore epoll_wait");
            break;
        }

        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == server_fd) {
                // 3️⃣ Accetta una nuova connessione
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1) {
                    perror("Errore accept");
                    continue;
                }
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
            } else {
                // 4️⃣ Gestisce la richiesta del client
                handle_client(events[i].data.fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
            }
        }
    }

    close(server_fd);
    return 0;
}
