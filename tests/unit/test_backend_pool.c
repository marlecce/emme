// tests/unit/test_backend_pool.c
// Unit tests for backend connection pool, health checker, and circuit breaker

#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <unistd.h>
#include <time.h>

#include "backend_pool.h"
#include "config.h"
#include "log.h"

static LoggingConfig test_log_config;

static void setup_logging(void)
{
    memset(&test_log_config, 0, sizeof(test_log_config));
    test_log_config.level = LOG_LEVEL_ERROR;
    test_log_config.format = LOG_FORMAT_PLAIN;
    test_log_config.buffer_size = 16384;
    test_log_config.rollover_size = 10485760;
    test_log_config.rollover_daily = 1;
    test_log_config.appender_flags = APPENDER_CONSOLE;
    snprintf(test_log_config.file, sizeof(test_log_config.file), "test_pool.log");
    log_init(&test_log_config);
}

static void teardown_logging(void)
{
    log_shutdown();
}

TestSuite(backend_pool, .init = setup_logging, .fini = teardown_logging);

Test(backend_pool, create_and_destroy)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 5);
    cr_assert_not_null(pool, "Pool should be created");
    
    cr_assert_eq(atomic_load(&pool->size), 5, "Pool should have 5 connections");
    cr_assert_eq(atomic_load(&pool->active_count), 0, "No active connections initially");
    cr_assert_eq(atomic_load(&pool->idle_count), 5, "All 5 connections should be idle");
    cr_assert_str_eq(pool->backend_host, "127.0.0.1", "Host should match");
    cr_assert_eq(pool->backend_port, 8080, "Port should match");
    cr_assert_eq(pool->tls_enabled, false, "TLS should be disabled");
    
    backend_pool_destroy(pool);
}

Test(backend_pool, acquire_and_release)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 3);
    cr_assert_not_null(pool, "Pool should be created");
    
    backend_conn_t *conn1 = backend_pool_acquire(pool);
    cr_assert_not_null(conn1, "Should acquire connection");
    cr_assert_eq(atomic_load(&pool->active_count), 1, "One active connection");
    cr_assert_eq(atomic_load(&pool->idle_count), 2, "Two idle connections");
    
    backend_conn_t *conn2 = backend_pool_acquire(pool);
    cr_assert_not_null(conn2, "Should acquire second connection");
    cr_assert_eq(atomic_load(&pool->active_count), 2, "Two active connections");
    
    backend_pool_release(conn1);
    cr_assert_eq(atomic_load(&pool->active_count), 1, "One active after release");
    cr_assert_eq(atomic_load(&pool->idle_count), 2, "Two idle after release");
    
    backend_pool_release(conn2);
    cr_assert_eq(atomic_load(&pool->active_count), 0, "No active after all released");
    
    backend_pool_destroy(pool);
}

Test(backend_pool, health_tracking)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    backend_conn_t *conn = backend_pool_acquire(pool);
    cr_assert_not_null(conn, "Should acquire connection");
    
    cr_assert_eq(conn->health, BACKEND_HEALTH_UNKNOWN, "Initial health should be unknown");
    
    backend_pool_mark_success(conn);
    cr_assert_eq(conn->health, BACKEND_HEALTH_UNKNOWN, "Still unknown after 1 success");
    cr_assert_eq(conn->consecutive_successes, 1, "One consecutive success");
    
    backend_pool_mark_success(conn);
    cr_assert_eq(conn->health, BACKEND_HEALTH_HEALTHY, "Should be healthy after 2 successes");
    cr_assert_eq(conn->consecutive_successes, 2, "Two consecutive successes");
    
    backend_pool_mark_failure(conn);
    backend_pool_mark_failure(conn);
    cr_assert_eq(conn->health, BACKEND_HEALTH_HEALTHY, "Still healthy after 2 failures");
    cr_assert_eq(conn->consecutive_failures, 2, "Two consecutive failures");
    
    backend_pool_mark_failure(conn);
    cr_assert_eq(conn->health, BACKEND_HEALTH_UNHEALTHY, "Should be unhealthy after 3 failures");
    cr_assert_eq(conn->consecutive_failures, 3, "Three consecutive failures");
    cr_assert_eq(conn->consecutive_successes, 0, "Successes should be reset");
    
    backend_pool_release(conn);
    backend_pool_destroy(pool);
}

Test(backend_pool, pool_exhaustion)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    backend_conn_t *conn1 = backend_pool_acquire(pool);
    backend_conn_t *conn2 = backend_pool_acquire(pool);
    
    cr_assert_not_null(conn1, "First acquire should succeed");
    cr_assert_not_null(conn2, "Second acquire should succeed");
    
    backend_conn_t *conn3 = backend_pool_acquire(pool);
    cr_assert_null(conn3, "Third acquire should fail (pool exhausted)");
    
    backend_pool_release(conn1);
    backend_conn_t *conn4 = backend_pool_acquire(pool);
    cr_assert_not_null(conn4, "Should acquire after release");
    
    backend_pool_release(conn2);
    backend_pool_release(conn4);
    backend_pool_destroy(pool);
}

TestSuite(circuit_breaker, .init = setup_logging, .fini = teardown_logging);

Test(circuit_breaker, init_and_destroy)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = true,
        .failure_threshold = 3,
        .recovery_timeout_seconds = 10
    };
    
    int result = backend_pool_init_circuit_breaker(pool, &config);
    cr_assert_eq(result, 0, "Circuit breaker should initialize");
    cr_assert_eq(pool->circuit_breaker_enabled, true, "Should be enabled");
    
    backend_pool_destroy_circuit_breaker(pool);
    cr_assert_eq(pool->circuit_breaker_enabled, false, "Should be disabled after destroy");
    
    backend_pool_destroy(pool);
}

Test(circuit_breaker, state_transitions_closed_to_open)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = true,
        .failure_threshold = 3,
        .recovery_timeout_seconds = 10
    };
    backend_pool_init_circuit_breaker(pool, &config);
    
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_CLOSED, "Initial state should be CLOSED");
    
    backend_pool_circuit_breaker_record_failure(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_CLOSED, "Still CLOSED after 1 failure");
    cr_assert_eq(backend_pool_circuit_breaker_get_failure_count(pool), 1, "Failure count = 1");
    
    backend_pool_circuit_breaker_record_failure(pool);
    backend_pool_circuit_breaker_record_failure(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_OPEN, "Should be OPEN after 3 failures");
    
    cr_assert_eq(backend_pool_circuit_breaker_get_total_opens(pool), 1, 
                 "Should have opened once");
    
    backend_pool_destroy_circuit_breaker(pool);
    backend_pool_destroy(pool);
}

Test(circuit_breaker, state_transitions_open_to_half_open)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = true,
        .failure_threshold = 2,
        .recovery_timeout_seconds = 1
    };
    backend_pool_init_circuit_breaker(pool, &config);
    
    backend_pool_circuit_breaker_record_failure(pool);
    backend_pool_circuit_breaker_record_failure(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_OPEN, "Should be OPEN");
    
    cr_assert_eq(backend_pool_circuit_breaker_allow_request(pool), false, 
                 "Should reject requests when OPEN");
    
    sleep(2);
    
    cr_assert_eq(backend_pool_circuit_breaker_allow_request(pool), true, 
                 "Should allow request after timeout (transition to HALF_OPEN)");
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_HALF_OPEN, "Should be HALF_OPEN");
    
    backend_pool_destroy_circuit_breaker(pool);
    backend_pool_destroy(pool);
}

Test(circuit_breaker, state_transitions_half_open_to_closed)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = true,
        .failure_threshold = 2,
        .recovery_timeout_seconds = 1
    };
    backend_pool_init_circuit_breaker(pool, &config);
    
    backend_pool_circuit_breaker_record_failure(pool);
    backend_pool_circuit_breaker_record_failure(pool);
    sleep(2);
    backend_pool_circuit_breaker_allow_request(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_HALF_OPEN, "Should be HALF_OPEN");
    
    backend_pool_circuit_breaker_record_success(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_HALF_OPEN, "Still HALF_OPEN after 1 success");
    
    backend_pool_circuit_breaker_record_success(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_CLOSED, "Should be CLOSED after 2 successes");
    
    cr_assert_eq(backend_pool_circuit_breaker_get_total_closes(pool), 1, 
                 "Should have closed once");
    
    backend_pool_destroy_circuit_breaker(pool);
    backend_pool_destroy(pool);
}

Test(circuit_breaker, state_transitions_half_open_to_open)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = true,
        .failure_threshold = 2,
        .recovery_timeout_seconds = 1
    };
    backend_pool_init_circuit_breaker(pool, &config);
    
    backend_pool_circuit_breaker_record_failure(pool);
    backend_pool_circuit_breaker_record_failure(pool);
    sleep(2);
    backend_pool_circuit_breaker_allow_request(pool);
    
    backend_pool_circuit_breaker_record_failure(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_OPEN, "Should be OPEN after failure in HALF_OPEN");
    
    cr_assert_eq(backend_pool_circuit_breaker_get_total_opens(pool), 2, 
                 "Should have opened twice");
    
    backend_pool_destroy_circuit_breaker(pool);
    backend_pool_destroy(pool);
}

Test(circuit_breaker, allow_request_when_closed)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = true,
        .failure_threshold = 5,
        .recovery_timeout_seconds = 10
    };
    backend_pool_init_circuit_breaker(pool, &config);
    
    cr_assert_eq(backend_pool_circuit_breaker_allow_request(pool), true, 
                 "Should allow requests when CLOSED");
    
    backend_pool_circuit_breaker_record_failure(pool);
    cr_assert_eq(backend_pool_circuit_breaker_allow_request(pool), true, 
                 "Should still allow requests before threshold");
    
    backend_pool_destroy_circuit_breaker(pool);
    backend_pool_destroy(pool);
}

Test(circuit_breaker, disabled_circuit_breaker)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 2);
    cr_assert_not_null(pool, "Pool should be created");
    
    circuit_breaker_config_t config = {
        .enabled = false,
        .failure_threshold = 3,
        .recovery_timeout_seconds = 10
    };
    int result = backend_pool_init_circuit_breaker(pool, &config);
    cr_assert_eq(result, -1, "Should fail to init disabled circuit breaker");
    
    cr_assert_eq(backend_pool_circuit_breaker_allow_request(pool), true, 
                 "Should always allow when disabled");
    
    backend_pool_circuit_breaker_record_failure(pool);
    cr_assert_eq(backend_pool_circuit_breaker_get_state(pool), 
                 CIRCUIT_BREAKER_CLOSED, "State should remain CLOSED");
    
    backend_pool_destroy(pool);
}

TestSuite(health_checker, .init = setup_logging, .fini = teardown_logging);

Test(health_checker, config_defaults)
{
    health_check_config_t config;
    memset(&config, 0, sizeof(config));
    
    cr_assert_eq(config.enabled, false, "Default enabled should be false");
    cr_assert_str_eq(config.path, "", "Default path should be empty");
    cr_assert_eq(config.interval_seconds, 0, "Default interval should be 0");
    
    config.enabled = true;
    snprintf(config.path, sizeof(config.path), "/health");
    config.interval_seconds = 10;
    config.timeout_seconds = 5;
    config.unhealthy_threshold = 3;
    config.healthy_threshold = 2;
    
    cr_assert_eq(config.enabled, true, "Should be enabled");
    cr_assert_str_eq(config.path, "/health", "Path should match");
    cr_assert_eq(config.interval_seconds, 10, "Interval should match");
}

Test(health_checker, health_status_enum)
{
    cr_assert_eq(BACKEND_HEALTH_UNKNOWN, 0, "UNKNOWN should be 0");
    cr_assert_eq(BACKEND_HEALTH_HEALTHY, 1, "HEALTHY should be 1");
    cr_assert_eq(BACKEND_HEALTH_UNHEALTHY, 2, "UNHEALTHY should be 2");
}

TestSuite(backend_pool_metrics, .init = setup_logging, .fini = teardown_logging);

Test(backend_pool_metrics, metrics_update)
{
    backend_pool_t *pool = backend_pool_create("127.0.0.1", 8080, false, false, 3);
    cr_assert_not_null(pool, "Pool should be created");
    
    backend_conn_t *conn = backend_pool_acquire(pool);
    cr_assert_not_null(conn, "Should acquire connection");
    
    backend_pool_update_metrics(pool);
    
    int active = atomic_load(&pool->active_count);
    int idle = atomic_load(&pool->idle_count);
    int healthy = backend_pool_get_healthy_count(pool);
    
    cr_assert_eq(active, 1, "Should have 1 active connection");
    cr_assert_eq(idle, 2, "Should have 2 idle connections");
    cr_assert_eq(healthy, 0, "No healthy connections initially");
    
    backend_pool_mark_success(conn);
    backend_pool_mark_success(conn);
    healthy = backend_pool_get_healthy_count(pool);
    cr_assert_eq(healthy, 1, "Should have 1 healthy connection after 2 successes");
    
    backend_pool_release(conn);
    backend_pool_destroy(pool);
}
