#ifndef SERVER_H
#define SERVER_H

#include "config.h"

#define BUFFER_SIZE 8192
#define QUEUE_DEPTH 64

void handle_client(int client_fd, ServerConfig *config);

int start_server(ServerConfig *config);

#endif