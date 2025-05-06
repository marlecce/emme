#ifndef SERVER_H
#define SERVER_H

#include "config.h"
#include <liburing.h>

#define BUFFER_SIZE 8192
#define QUEUE_DEPTH 64

void handle_client(int client_fd, ServerConfig *config, struct io_uring *ring);

int start_server(ServerConfig *config);

#endif