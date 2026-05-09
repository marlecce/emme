#ifndef CONFIG_H
#define CONFIG_H

#define MAX_ROUTES 16
#define MAX_LOG_LEVEL 16

#include <limits.h>
#include "logging_common.h"

typedef struct {
    char path[128];       
    char technology[32];   
    char document_root[256];
    char document_root_real[PATH_MAX];
    int document_root_resolved;
    char backend[64];
} Route;

typedef struct {
    char certificate[256];
    char private_key[256];
    char session_ticket_key[256];
    int session_cache_size;
    int session_timeout;
} SSLConfig;

typedef struct {
    int keepalive_timeout;
    int max_requests_per_connection;
    int max_concurrent_streams;
} HTTP2Config;

typedef struct {
    int port;
    int max_connections;
    char log_level[MAX_LOG_LEVEL];
    int route_count;
    Route routes[MAX_ROUTES];
    LoggingConfig logging;
    SSLConfig ssl;
    HTTP2Config http2;
} ServerConfig;

int load_config(ServerConfig *config, const char *file_path);

#endif
