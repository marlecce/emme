#ifndef ROUTER_H
#define ROUTER_H

#include "http_parser.h"
#include "config.h"

int route_request(HttpRequest *req, char *raw_request, int req_len, ServerConfig *config, int client_fd);

#endif