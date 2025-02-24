#ifndef CONFIG_H
#define CONFIG_H

#define MAX_ROUTES 16
#define MAX_LOG_LEVEL 16

typedef struct {
    char path[128];       
    char technology[32];   
    char document_root[256];
    char backend[64];
} Route;

typedef struct {
    int port;
    int max_connections;
    char log_level[MAX_LOG_LEVEL];
    int route_count;
    Route routes[MAX_ROUTES];
} ServerConfig;

int load_config(ServerConfig *config, const char *file_path);

#endif
