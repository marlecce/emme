#ifndef SERVER_H
#define SERVER_H

#include "config.h"

void handle_client(int client_fd, ServerConfig *config);

int start_server(ServerConfig *config);

#endif