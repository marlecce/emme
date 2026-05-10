/* backend_pool.c - Backend connection pooling for HTTP/2 reverse proxy
 *
 * This module manages a pool of reusable HTTP/2 connections to backend servers.
 * Features:
 *   - Fixed-size connection pool with mutex protection
 *   - Connection reuse across multiple requests
 *   - Health tracking per connection
 *   - Idle timeout for connection cleanup
 *   - Thread-safe acquisition/release
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "backend_pool.h"
#include "log.h"
#include "http2_client.h"
#include "metrics.h"
#include "http_status.h"

#ifndef DEBUG_H2C
#define DEBUG_H2C 0
#endif

#define H2C_LOG(...)                         \
    do {                                     \
        if (DEBUG_H2C)                       \
            log_message(LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } while (0)

// Create a single backend connection
static backend_conn_t* create_backend_connection(backend_pool_t *pool, const backend_config_t *config)
{
    backend_conn_t *conn = calloc(1, sizeof(backend_conn_t));
    if (!conn) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate backend connection");
        return NULL;
    }
    
    if (pthread_mutex_init(&conn->lock, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to initialize connection mutex");
        free(conn);
        return NULL;
    }
    
    conn->pool = pool;
    atomic_store(&conn->in_use, false);
    atomic_store(&conn->last_used, time(NULL));
    conn->health = BACKEND_HEALTH_UNKNOWN;
    conn->consecutive_failures = 0;
    conn->consecutive_successes = 0;
    
    if (http2_client_init(&conn->client, config) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to initialize HTTP/2 client");
        pthread_mutex_destroy(&conn->lock);
        free(conn);
        return NULL;
    }
    
    return conn;
}

// Destroy a single backend connection
static void destroy_backend_connection(backend_conn_t *conn)
{
    if (!conn) return;
    
    http2_client_cleanup(&conn->client);
    pthread_mutex_destroy(&conn->lock);
    free(conn);
}

// Connect a backend connection to the server
static int connect_backend_connection(backend_conn_t *conn, const backend_config_t *config)
{
    if (!conn) return -1;
    return http2_client_connect(&conn->client, config);
}

backend_pool_t* backend_pool_create(const char *host, int port,
                                     bool tls_enabled, bool tls_verify,
                                     int pool_size)
{
    if (!host || pool_size <= 0 || pool_size > BACKEND_POOL_MAX_SIZE) {
        log_message(LOG_LEVEL_ERROR, "Invalid backend pool parameters");
        return NULL;
    }
    
    backend_pool_t *pool = calloc(1, sizeof(backend_pool_t));
    if (!pool) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate backend pool");
        return NULL;
    }
    
    if (pthread_mutex_init(&pool->pool_lock, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to initialize pool mutex");
        free(pool);
        return NULL;
    }
    
    strncpy(pool->backend_host, host, sizeof(pool->backend_host) - 1);
    pool->backend_port = port;
    pool->tls_enabled = tls_enabled;
    pool->tls_verify = tls_verify;
    pool->idle_timeout_sec = BACKEND_POOL_IDLE_TIMEOUT_SEC;
    atomic_store(&pool->size, 0);
    atomic_store(&pool->active_count, 0);
    atomic_store(&pool->idle_count, 0);
    atomic_store(&pool->healthy_count, 0);
    
    pool->config.host[sizeof(pool->config.host) - 1] = '\0';
    strncpy(pool->config.host, host, sizeof(pool->config.host) - 1);
    pool->config.port = port;
    pool->config.tls_enabled = tls_enabled;
    pool->config.tls_verify = tls_verify;
    
    // Pre-create connections
    for (int i = 0; i < pool_size; i++) {
        backend_conn_t *conn = create_backend_connection(pool, &pool->config);
        if (!conn) {
            log_message(LOG_LEVEL_WARN, "Failed to create connection %d for %s:%d", 
                        i, host, port);
            break;
        }
        
        pool->connections[i] = conn;
        atomic_fetch_add(&pool->size, 1);
        atomic_fetch_add(&pool->idle_count, 1);
        
        log_message(LOG_LEVEL_INFO, "Created backend connection %d for %s:%d", 
                    i, host, port);
    }
    
    log_message(LOG_LEVEL_INFO, "Backend pool created for %s:%d (size=%d, tls=%s)", 
                host, port, atomic_load(&pool->size), tls_enabled ? "yes" : "no");
    
    return pool;
}

void backend_pool_destroy(backend_pool_t *pool)
{
    if (!pool) return;
    
    log_message(LOG_LEVEL_INFO, "Destroying backend pool for %s:%d", 
                pool->backend_host, pool->backend_port);
    
    pthread_mutex_lock(&pool->pool_lock);
    
    int size = atomic_load(&pool->size);
    for (int i = 0; i < size; i++) {
        if (pool->connections[i]) {
            destroy_backend_connection(pool->connections[i]);
            pool->connections[i] = NULL;
        }
    }
    
    pthread_mutex_unlock(&pool->pool_lock);
    pthread_mutex_destroy(&pool->pool_lock);
    free(pool);
}

backend_conn_t* backend_pool_acquire(backend_pool_t *pool)
{
    if (!pool) return NULL;
    
    pthread_mutex_lock(&pool->pool_lock);
    
    int size = atomic_load(&pool->size);
    time_t now = time(NULL);
    
    // Find an idle, healthy connection
    for (int i = 0; i < size; i++) {
        backend_conn_t *conn = pool->connections[i];
        if (!conn) continue;
        
        pthread_mutex_lock(&conn->lock);
        
        bool in_use = atomic_load(&conn->in_use);
        if (!in_use && conn->health != BACKEND_HEALTH_UNHEALTHY) {
            // Check idle timeout
            long last_used = atomic_load(&conn->last_used);
            if (now - last_used > pool->idle_timeout_sec) {
                log_message(LOG_LEVEL_INFO, "Connection %d idle timeout, reconnecting", i);
                http2_client_cleanup(&conn->client);
                if (http2_client_init(&conn->client, &pool->config) == 0) {
                    if (connect_backend_connection(conn, &pool->config) == 0) {
                        atomic_store(&conn->last_used, now);
                    } else {
                        conn->health = BACKEND_HEALTH_UNHEALTHY;
                        pthread_mutex_unlock(&conn->lock);
                        continue;
                    }
                } else {
                    conn->health = BACKEND_HEALTH_UNHEALTHY;
                    pthread_mutex_unlock(&conn->lock);
                    continue;
                }
            }
            
            atomic_store(&conn->in_use, true);
            atomic_fetch_add(&pool->active_count, 1);
            atomic_fetch_sub(&pool->idle_count, 1);
            
            pthread_mutex_unlock(&conn->lock);
            pthread_mutex_unlock(&pool->pool_lock);
            
            H2C_LOG("backend_pool: acquired connection %d", i);
            return conn;
        }
        
        pthread_mutex_unlock(&conn->lock);
    }
    
    pthread_mutex_unlock(&pool->pool_lock);
    
    log_message(LOG_LEVEL_WARN, "No available connections in pool for %s:%d", 
                pool->backend_host, pool->backend_port);
    return NULL;
}

void backend_pool_release(backend_conn_t *conn)
{
    if (!conn || !conn->pool) return;
    
    backend_pool_t *pool = conn->pool;
    
    pthread_mutex_lock(&conn->lock);
    
    atomic_store(&conn->in_use, false);
    atomic_store(&conn->last_used, time(NULL));
    
    pthread_mutex_unlock(&conn->lock);
    
    atomic_fetch_sub(&pool->active_count, 1);
    atomic_fetch_add(&pool->idle_count, 1);
    
    H2C_LOG("backend_pool: released connection");
}

void backend_pool_mark_success(backend_conn_t *conn)
{
    if (!conn) return;
    
    pthread_mutex_lock(&conn->lock);
    
    conn->consecutive_failures = 0;
    conn->consecutive_successes++;
    
    if (conn->consecutive_successes >= 2 && conn->health != BACKEND_HEALTH_HEALTHY) {
        conn->health = BACKEND_HEALTH_HEALTHY;
        log_message(LOG_LEVEL_INFO, "Backend connection marked healthy");
    }
    
    pthread_mutex_unlock(&conn->lock);
}

void backend_pool_mark_failure(backend_conn_t *conn)
{
    if (!conn) return;
    
    pthread_mutex_lock(&conn->lock);
    
    conn->consecutive_successes = 0;
    conn->consecutive_failures++;
    
    if (conn->consecutive_failures >= 3) {
        conn->health = BACKEND_HEALTH_UNHEALTHY;
        log_message(LOG_LEVEL_WARN, "Backend connection marked unhealthy after %d failures", 
                    conn->consecutive_failures);
    }
    
    pthread_mutex_unlock(&conn->lock);
}

backend_health_t backend_pool_get_health(backend_conn_t *conn)
{
    if (!conn) return BACKEND_HEALTH_UNKNOWN;
    
    pthread_mutex_lock(&conn->lock);
    backend_health_t health = conn->health;
    pthread_mutex_unlock(&conn->lock);
    
    return health;
}

int backend_pool_get_active_count(backend_pool_t *pool)
{
    if (!pool) return 0;
    return atomic_load(&pool->active_count);
}

int backend_pool_get_idle_count(backend_pool_t *pool)
{
    if (!pool) return 0;
    return atomic_load(&pool->idle_count);
}

int backend_pool_get_healthy_count(backend_pool_t *pool)
{
    if (!pool) return 0;
    
    int healthy = 0;
    int size = atomic_load(&pool->size);
    
    for (int i = 0; i < size; i++) {
        backend_conn_t *conn = pool->connections[i];
        if (conn && conn->health == BACKEND_HEALTH_HEALTHY) {
            healthy++;
        }
    }
    
    return healthy;
}

bool backend_pool_has_healthy_connection(backend_pool_t *pool)
{
    return backend_pool_get_healthy_count(pool) > 0;
}

static void handle_health_check_success(health_checker_t *checker, backend_conn_t *conn)
{
    int prev_failures = atomic_load(&checker->consecutive_failures);
    atomic_store(&checker->consecutive_failures, 0);
    int successes = atomic_fetch_add(&checker->consecutive_successes, 1) + 1;
    
    backend_pool_mark_success(conn);
    
    if (prev_failures >= checker->config.unhealthy_threshold && 
        successes >= checker->config.healthy_threshold) {
        backend_health_t old_health = atomic_load(&checker->health);
        if (old_health != BACKEND_HEALTH_HEALTHY) {
            atomic_store(&checker->health, BACKEND_HEALTH_HEALTHY);
            log_message(LOG_LEVEL_INFO, "Health check: backend marked HEALTHY");
        }
    }
}

static void handle_health_check_failure(health_checker_t *checker, backend_conn_t *conn)
{
    int prev_successes = atomic_load(&checker->consecutive_successes);
    atomic_store(&checker->consecutive_successes, 0);
    int failures = atomic_fetch_add(&checker->consecutive_failures, 1) + 1;
    
    backend_pool_mark_failure(conn);
    
    if (prev_successes >= checker->config.healthy_threshold &&
        failures >= checker->config.unhealthy_threshold) {
        backend_health_t old_health = atomic_load(&checker->health);
        if (old_health != BACKEND_HEALTH_UNHEALTHY) {
            atomic_store(&checker->health, BACKEND_HEALTH_UNHEALTHY);
            log_message(LOG_LEVEL_WARN, "Health check: backend marked UNHEALTHY after %d failures",
                       failures);
        }
    }
}

static bool perform_health_check(health_checker_t *checker, backend_conn_t *conn)
{
    const char *health_path = checker->config.path;
    if (health_path[0] == '\0') {
        health_path = "/health";
    }
    
    int status = http2_client_send_request(&conn->client, "GET", 
                                           health_path, "health-check", NULL, 0);
    
    if (status >= 0) {
        int response_status = http2_client_recv_response(&conn->client);
        if (response_status >= HTTP_STATUS_SUCCESS_MIN && response_status < HTTP_STATUS_CLIENT_ERROR_MIN) {
            return true;
        } else {
            log_message(LOG_LEVEL_WARN, "Health check: backend returned status %d", 
                       response_status);
        }
    } else {
        log_message(LOG_LEVEL_WARN, "Health check: failed to send/receive");
    }
    return false;
}

static void *health_check_thread(void *arg)
{
    health_checker_t *checker = (health_checker_t *)arg;
    if (!checker || !checker->pool) {
        return NULL;
    }
    
    log_message(LOG_LEVEL_DEBUG, "Health check thread started");
    
    while (atomic_load(&checker->state) == HEALTH_CHECK_STATE_RUNNING) {
        backend_conn_t *conn = backend_pool_acquire(checker->pool);
        if (!conn) {
            log_message(LOG_LEVEL_WARN, "Health check: failed to acquire connection");
            atomic_fetch_add(&checker->consecutive_failures, 1);
            atomic_store(&checker->consecutive_successes, 0);
            atomic_store(&checker->health, BACKEND_HEALTH_UNHEALTHY);
            atomic_fetch_add(&checker->failed_checks, 1);
            atomic_fetch_add(&checker->total_checks, 1);
            atomic_store(&checker->last_check_time, (long)time(NULL));
            sleep(checker->config.interval_seconds);
            continue;
        }
        
        bool check_success = perform_health_check(checker, conn);
        
        if (check_success) {
            handle_health_check_success(checker, conn);
        } else {
            handle_health_check_failure(checker, conn);
        }
        
        atomic_fetch_add(&checker->total_checks, 1);
        if (!check_success) {
            atomic_fetch_add(&checker->failed_checks, 1);
        }
        atomic_store(&checker->last_check_time, (long)time(NULL));
        
        backend_pool_release(conn);
        backend_pool_update_metrics(checker->pool);
        sleep(checker->config.interval_seconds);
    }
    
    log_message(LOG_LEVEL_DEBUG, "Health check thread stopped");
    return NULL;
}

static int health_checker_start(health_checker_t *checker, backend_pool_t *pool, 
                                health_check_config_t *config)
{
    if (!checker || !pool || !config || !config->enabled) {
        return -1;
    }
    
    memset(checker, 0, sizeof(*checker));
    checker->pool = pool;
    memcpy(&checker->config, config, sizeof(*config));
    atomic_store(&checker->state, HEALTH_CHECK_STATE_RUNNING);
    atomic_store(&checker->health, BACKEND_HEALTH_UNKNOWN);
    atomic_store(&checker->last_check_time, 0);
    
    if (pthread_create(&checker->thread, NULL, health_check_thread, checker) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create health check thread");
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "Health checker started (interval=%ds, path=%s)",
               checker->config.interval_seconds, checker->config.path);
    return 0;
}

static void health_checker_stop(health_checker_t *checker)
{
    if (!checker) {
        return;
    }
    
    atomic_store(&checker->state, HEALTH_CHECK_STATE_STOPPED);
    
    if (checker->thread) {
        pthread_join(checker->thread, NULL);
    }
    
    log_message(LOG_LEVEL_DEBUG, "Health checker stopped");
}

static backend_health_t health_checker_get_health(health_checker_t *checker)
{
    if (!checker) {
        return BACKEND_HEALTH_UNKNOWN;
    }
    return atomic_load(&checker->health);
}

int backend_pool_start_health_checker(backend_pool_t *pool, health_check_config_t *config)
{
    if (!pool || !config || !config->enabled) {
        return -1;
    }
    
    pool->health_check_enabled = true;
    
    if (health_checker_start(&pool->health_checker, pool, config) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to start health checker for %s:%d",
                   pool->backend_host, pool->backend_port);
        return -1;
    }
    
    return 0;
}

void backend_pool_stop_health_checker(backend_pool_t *pool)
{
    if (!pool || !pool->health_check_enabled) {
        return;
    }
    
    health_checker_stop(&pool->health_checker);
    pool->health_check_enabled = false;
}

backend_health_t backend_pool_get_overall_health(backend_pool_t *pool)
{
    if (!pool) {
        return BACKEND_HEALTH_UNKNOWN;
    }
    
    if (pool->health_check_enabled) {
        return health_checker_get_health(&pool->health_checker);
    }
    
    return backend_pool_get_healthy_count(pool) > 0 ? 
           BACKEND_HEALTH_HEALTHY : BACKEND_HEALTH_UNKNOWN;
}

void backend_pool_update_metrics(backend_pool_t *pool)
{
    if (!pool) {
        return;
    }
    
    int active = atomic_load(&pool->active_count);
    int idle = atomic_load(&pool->idle_count);
    int healthy = backend_pool_get_healthy_count(pool);
    
    metrics_set_backend_pool_stats(pool->backend_host, active, idle, healthy);
    
    if (pool->health_check_enabled) {
        health_checker_t *hc = &pool->health_checker;
        metrics_set_health_checker_stats(pool->backend_host,
                                         (int)atomic_load(&hc->total_checks),
                                         (int)atomic_load(&hc->failed_checks),
                                         (int)atomic_load(&hc->health),
                                         atomic_load(&hc->last_check_time));
    }
    
    if (pool->circuit_breaker_enabled) {
        circuit_breaker_t *cb = &pool->circuit_breaker;
        metrics_set_circuit_breaker_stats(pool->backend_host,
                                          (int)atomic_load(&cb->state),
                                          atomic_load(&cb->failure_count),
                                          atomic_load(&cb->total_opens),
                                          atomic_load(&cb->total_closes));
    }
}

int backend_pool_init_circuit_breaker(backend_pool_t *pool, circuit_breaker_config_t *config)
{
    if (!pool || !config || !config->enabled) {
        return -1;
    }
    
    memset(&pool->circuit_breaker, 0, sizeof(pool->circuit_breaker));
    memcpy(&pool->circuit_breaker.config, config, sizeof(*config));
    atomic_store(&pool->circuit_breaker.state, CIRCUIT_BREAKER_CLOSED);
    atomic_store(&pool->circuit_breaker.failure_count, 0);
    atomic_store(&pool->circuit_breaker.success_count, 0);
    atomic_store(&pool->circuit_breaker.last_failure_time, 0);
    atomic_store(&pool->circuit_breaker.last_state_change, time(NULL));
    atomic_store(&pool->circuit_breaker.total_opens, 0);
    atomic_store(&pool->circuit_breaker.total_closes, 0);
    pool->circuit_breaker_enabled = true;
    
    log_message(LOG_LEVEL_INFO, "Circuit breaker initialized for %s:%d (threshold=%d, recovery=%ds)",
               pool->backend_host, pool->backend_port, 
               config->failure_threshold, config->recovery_timeout_seconds);
    return 0;
}

void backend_pool_destroy_circuit_breaker(backend_pool_t *pool)
{
    if (!pool || !pool->circuit_breaker_enabled) {
        return;
    }
    
    pool->circuit_breaker_enabled = false;
    log_message(LOG_LEVEL_DEBUG, "Circuit breaker destroyed for %s:%d",
               pool->backend_host, pool->backend_port);
}

bool backend_pool_circuit_breaker_allow_request(backend_pool_t *pool)
{
    if (!pool || !pool->circuit_breaker_enabled) {
        return true;
    }
    
    circuit_breaker_state_t state = atomic_load(&pool->circuit_breaker.state);
    
    if (state == CIRCUIT_BREAKER_CLOSED) {
        return true;
    }
    
    if (state == CIRCUIT_BREAKER_OPEN) {
        long now = time(NULL);
        long last_failure = atomic_load(&pool->circuit_breaker.last_failure_time);
        int recovery_timeout = pool->circuit_breaker.config.recovery_timeout_seconds;
        
        if (now - last_failure >= recovery_timeout) {
            atomic_store(&pool->circuit_breaker.state, CIRCUIT_BREAKER_HALF_OPEN);
            atomic_store(&pool->circuit_breaker.last_state_change, now);
            log_message(LOG_LEVEL_INFO, "Circuit breaker transitioning to HALF-OPEN for %s:%d",
                       pool->backend_host, pool->backend_port);
            return true;
        }
        return false;
    }
    
    if (state == CIRCUIT_BREAKER_HALF_OPEN) {
        return true;
    }
    
    return false;
}

void backend_pool_circuit_breaker_record_success(backend_pool_t *pool)
{
    if (!pool || !pool->circuit_breaker_enabled) {
        return;
    }
    
    circuit_breaker_state_t state = atomic_load(&pool->circuit_breaker.state);
    
    if (state == CIRCUIT_BREAKER_HALF_OPEN) {
        int successes = atomic_fetch_add(&pool->circuit_breaker.success_count, 1) + 1;
        if (successes >= 2) {
            atomic_store(&pool->circuit_breaker.state, CIRCUIT_BREAKER_CLOSED);
            atomic_store(&pool->circuit_breaker.failure_count, 0);
            atomic_store(&pool->circuit_breaker.success_count, 0);
            atomic_store(&pool->circuit_breaker.last_state_change, time(NULL));
            atomic_fetch_add(&pool->circuit_breaker.total_closes, 1);
            log_message(LOG_LEVEL_INFO, "Circuit breaker CLOSED for %s:%d after successful recovery",
                       pool->backend_host, pool->backend_port);
        }
    } else if (state == CIRCUIT_BREAKER_CLOSED) {
        atomic_store(&pool->circuit_breaker.failure_count, 0);
    }
}

void backend_pool_circuit_breaker_record_failure(backend_pool_t *pool)
{
    if (!pool || !pool->circuit_breaker_enabled) {
        return;
    }
    
    circuit_breaker_state_t state = atomic_load(&pool->circuit_breaker.state);
    long now = time(NULL);
    
    if (state == CIRCUIT_BREAKER_CLOSED || state == CIRCUIT_BREAKER_HALF_OPEN) {
        int failures = atomic_fetch_add(&pool->circuit_breaker.failure_count, 1) + 1;
        atomic_store(&pool->circuit_breaker.last_failure_time, now);
        
        if (state == CIRCUIT_BREAKER_HALF_OPEN) {
            atomic_store(&pool->circuit_breaker.state, CIRCUIT_BREAKER_OPEN);
            atomic_store(&pool->circuit_breaker.last_state_change, now);
            atomic_fetch_add(&pool->circuit_breaker.total_opens, 1);
            log_message(LOG_LEVEL_WARN, "Circuit breaker OPENED for %s:%d (failure in half-open state)",
                       pool->backend_host, pool->backend_port);
        } else if (failures >= pool->circuit_breaker.config.failure_threshold) {
            atomic_store(&pool->circuit_breaker.state, CIRCUIT_BREAKER_OPEN);
            atomic_store(&pool->circuit_breaker.last_state_change, now);
            atomic_fetch_add(&pool->circuit_breaker.total_opens, 1);
            log_message(LOG_LEVEL_WARN, "Circuit breaker OPENED for %s:%d after %d failures",
                       pool->backend_host, pool->backend_port, failures);
        }
    }
}

circuit_breaker_state_t backend_pool_circuit_breaker_get_state(backend_pool_t *pool)
{
    if (!pool || !pool->circuit_breaker_enabled) {
        return CIRCUIT_BREAKER_CLOSED;
    }
    return atomic_load(&pool->circuit_breaker.state);
}

int backend_pool_circuit_breaker_get_failure_count(backend_pool_t *pool)
{
    if (!pool) {
        return 0;
    }
    return atomic_load(&pool->circuit_breaker.failure_count);
}

long backend_pool_circuit_breaker_get_total_opens(backend_pool_t *pool)
{
    if (!pool) {
        return 0;
    }
    return atomic_load(&pool->circuit_breaker.total_opens);
}

long backend_pool_circuit_breaker_get_total_closes(backend_pool_t *pool)
{
    if (!pool) {
        return 0;
    }
    return atomic_load(&pool->circuit_breaker.total_closes);
}
