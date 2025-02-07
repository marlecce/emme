#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int port;
    int max_connections;
    char log_level[10];
} ServerConfig;

int load_config(ServerConfig *config, const char *file_path);

#endif
