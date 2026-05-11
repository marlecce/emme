#ifndef CONFIG_H
#define CONFIG_H

#define MAX_ROUTES 16
#define MAX_LOG_LEVEL 16
#define BACKEND_POOL_DEFAULT_SIZE 10
#define BACKEND_POOL_IDLE_TIMEOUT_SEC 60

#include <limits.h>
#include <stdbool.h>
#include "logging_common.h"

typedef struct {
    bool enabled;
    char path[128];
    int interval_seconds;
    int timeout_seconds;
    int unhealthy_threshold;
    int healthy_threshold;
} HealthCheckConfig;

typedef struct {
    int size;
    int idle_timeout_seconds;
} ConnectionPoolConfig;

typedef struct {
    bool enabled;
    int failure_threshold;
    int recovery_timeout_seconds;
} CircuitBreakerConfig;

typedef struct backend_pool_s RouteBackendPool;

typedef struct {
    char path[128];       
    char technology[32];   
    char document_root[256];
    char document_root_real[PATH_MAX];
    int document_root_resolved;
    char backend[64];
    bool http2_enabled;
    bool tls_enabled;
    bool tls_verify;
    HealthCheckConfig health_check;
    ConnectionPoolConfig connection_pool;
    CircuitBreakerConfig circuit_breaker;
    RouteBackendPool *pool;
} Route;

typedef struct {
    char certificate[256];
    char private_key[256];
    char session_ticket_key[256];
    int session_cache_size;
    int session_timeout;
    int read_buffer_size;
    int enable_partial_write;
    int release_buffers;
} SSLConfig;

typedef struct {
    int keepalive_timeout;
    int max_requests_per_connection;
    int max_concurrent_streams;
} HTTP2Config;

typedef struct {
    int port;
    int max_connections;
    int shutdown_timeout_seconds;
    int request_timeout_ms;
    int tls_handshake_timeout_ms;
    char log_level[MAX_LOG_LEVEL];
    int route_count;
    Route routes[MAX_ROUTES];
    LoggingConfig logging;
    SSLConfig ssl;
    HTTP2Config http2;
} ServerConfig;

int load_config(ServerConfig *config, const char *file_path);
void apply_env_overrides(ServerConfig *config);
int parse_backend_url(const char *backend, char *host, size_t host_size, int *port);

#endif
