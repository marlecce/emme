#ifndef CONFIG_H
#define CONFIG_H

#define MAX_ROUTES 16
#define MAX_LOG_LEVEL 16

#include "logging_common.h"

typedef struct {
    char path[128];       
    char technology[32];   
    char document_root[256];
    char backend[64];
} Route;

typedef struct {
    char certificate[256];
    char private_key[256];
} SSLConfig;

typedef struct {
    int port;
    int max_connections;
    char log_level[MAX_LOG_LEVEL];
    int route_count;
    Route routes[MAX_ROUTES];
    LoggingConfig logging;
    SSLConfig ssl;
} ServerConfig;

int load_config(ServerConfig *config, const char *file_path);

#endif
