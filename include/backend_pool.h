#ifndef BACKEND_POOL_H
#define BACKEND_POOL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "config.h"
#include "http2_client.h"

#define BACKEND_POOL_MAX_SIZE 20
#define BACKEND_POOL_DEFAULT_SIZE 10
#define BACKEND_POOL_IDLE_TIMEOUT_SEC 60

#define BACKEND_POOL_HEALTHY_THRESHOLD      2
#define BACKEND_POOL_UNHEALTHY_THRESHOLD    3
#define BACKEND_POOL_DEFAULT_INTERVAL_SEC   10
#define BACKEND_POOL_DEFAULT_TIMEOUT_SEC    5

#define CIRCUIT_BREAKER_FAILURE_THRESHOLD   5
#define CIRCUIT_BREAKER_RECOVERY_TIMEOUT_SEC 30
#define CIRCUIT_BREAKER_SUCCESS_THRESHOLD   2

typedef enum {
    BACKEND_HEALTH_UNKNOWN = 0,
    BACKEND_HEALTH_HEALTHY = 1,
    BACKEND_HEALTH_UNHEALTHY = 2
} backend_health_t;

typedef enum {
    HEALTH_CHECK_STATE_STOPPED = 0,
    HEALTH_CHECK_STATE_RUNNING = 1
} health_check_state_t;

typedef enum {
    CIRCUIT_BREAKER_CLOSED = 0,
    CIRCUIT_BREAKER_OPEN = 1,
    CIRCUIT_BREAKER_HALF_OPEN = 2
} circuit_breaker_state_t;

typedef struct backend_pool_s backend_pool_t;

typedef HealthCheckConfig health_check_config_t;
typedef CircuitBreakerConfig circuit_breaker_config_t;

typedef struct {
    _Atomic circuit_breaker_state_t state;
    _Atomic int failure_count;
    _Atomic int success_count;
    _Atomic long last_failure_time;
    _Atomic long last_state_change;
    _Atomic long total_opens;
    _Atomic long total_closes;
    circuit_breaker_config_t config;
} circuit_breaker_t;

typedef struct {
    struct backend_pool_s *pool;
    health_check_config_t config;
    pthread_t thread;
    _Atomic health_check_state_t state;
    _Atomic int consecutive_failures;
    _Atomic int consecutive_successes;
    _Atomic backend_health_t health;
    _Atomic long last_check_time;
    _Atomic long total_checks;
    _Atomic long failed_checks;
} health_checker_t;

typedef struct {
    struct backend_pool_s *pool;
    http2_client_t client;
    _Atomic bool in_use;
    _Atomic long last_used;
    backend_health_t health;
    int consecutive_failures;
    int consecutive_successes;
    pthread_mutex_t lock;
} backend_conn_t;

typedef struct backend_pool_s {
    backend_conn_t *connections[BACKEND_POOL_MAX_SIZE];
    _Atomic int size;
    _Atomic int active_count;
    _Atomic int idle_count;
    _Atomic int healthy_count;
    char backend_host[256];
    int backend_port;
    bool tls_enabled;
    bool tls_verify;
    int idle_timeout_sec;
    pthread_mutex_t pool_lock;
    backend_config_t config;
    health_checker_t health_checker;
    bool health_check_enabled;
    circuit_breaker_t circuit_breaker;
    bool circuit_breaker_enabled;
} backend_pool_t;

// Pool lifecycle
backend_pool_t* backend_pool_create(const char *host, int port, 
                                     bool tls_enabled, bool tls_verify,
                                     int pool_size);
void backend_pool_destroy(backend_pool_t *pool);

// Connection acquisition
backend_conn_t* backend_pool_acquire(backend_pool_t *pool);
void backend_pool_release(backend_conn_t *conn);

// Health tracking
void backend_pool_mark_success(backend_conn_t *conn);
void backend_pool_mark_failure(backend_conn_t *conn);
backend_health_t backend_pool_get_health(backend_conn_t *conn);

// Pool statistics
int backend_pool_get_active_count(backend_pool_t *pool);
int backend_pool_get_idle_count(backend_pool_t *pool);
int backend_pool_get_healthy_count(backend_pool_t *pool);
bool backend_pool_has_healthy_connection(backend_pool_t *pool);

// Health checker
int backend_pool_start_health_checker(backend_pool_t *pool, health_check_config_t *config);
void backend_pool_stop_health_checker(backend_pool_t *pool);
backend_health_t backend_pool_get_overall_health(backend_pool_t *pool);

// Circuit breaker
int backend_pool_init_circuit_breaker(backend_pool_t *pool, circuit_breaker_config_t *config);
void backend_pool_destroy_circuit_breaker(backend_pool_t *pool);
bool backend_pool_circuit_breaker_allow_request(backend_pool_t *pool);
void backend_pool_circuit_breaker_record_success(backend_pool_t *pool);
void backend_pool_circuit_breaker_record_failure(backend_pool_t *pool);
circuit_breaker_state_t backend_pool_circuit_breaker_get_state(backend_pool_t *pool);
int backend_pool_circuit_breaker_get_failure_count(backend_pool_t *pool);
long backend_pool_circuit_breaker_get_total_opens(backend_pool_t *pool);
long backend_pool_circuit_breaker_get_total_closes(backend_pool_t *pool);

// Metrics
void backend_pool_update_metrics(backend_pool_t *pool);

#endif // BACKEND_POOL_H
