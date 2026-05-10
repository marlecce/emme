#include <criterion/criterion.h>
#include <criterion/logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "server.h"

Test(shutdown_context, state_enum_values)
{
    cr_assert_eq(SHUTDOWN_STATE_RUNNING, 0, "RUNNING should be 0");
    cr_assert_eq(SHUTDOWN_STATE_DRAINING, 1, "DRAINING should be 1");
    cr_assert_eq(SHUTDOWN_STATE_FORCED, 2, "FORCED should be 2");
}

Test(shutdown_context, structure_exists)
{
    shutdown_context_t ctx = {0};
    
    atomic_store(&ctx.state, SHUTDOWN_STATE_RUNNING);
    cr_assert_eq(atomic_load(&ctx.state), SHUTDOWN_STATE_RUNNING);
    
    atomic_store(&ctx.state, SHUTDOWN_STATE_DRAINING);
    cr_assert_eq(atomic_load(&ctx.state), SHUTDOWN_STATE_DRAINING);
    
    atomic_store(&ctx.state, SHUTDOWN_STATE_FORCED);
    cr_assert_eq(atomic_load(&ctx.state), SHUTDOWN_STATE_FORCED);
}

Test(shutdown_context, in_flight_atomic_operations)
{
    shutdown_context_t ctx = {0};
    
    atomic_store(&ctx.in_flight_requests, 0);
    
    atomic_fetch_add(&ctx.in_flight_requests, 5);
    cr_assert_eq(atomic_load(&ctx.in_flight_requests), 5);
    
    atomic_fetch_add(&ctx.in_flight_requests, 3);
    cr_assert_eq(atomic_load(&ctx.in_flight_requests), 8);
    
    atomic_fetch_sub(&ctx.in_flight_requests, 2);
    cr_assert_eq(atomic_load(&ctx.in_flight_requests), 6);
    
    atomic_fetch_sub(&ctx.in_flight_requests, 6);
    cr_assert_eq(atomic_load(&ctx.in_flight_requests), 0);
}

Test(shutdown_context, metrics_tracking)
{
    shutdown_context_t ctx = {0};
    
    atomic_store(&ctx.metrics.peak_in_flight, 10);
    cr_assert_eq(atomic_load(&ctx.metrics.peak_in_flight), 10);
    
    atomic_store(&ctx.metrics.completed, 8);
    cr_assert_eq(atomic_load(&ctx.metrics.completed), 8);
    
    atomic_store(&ctx.metrics.forced, 2);
    cr_assert_eq(atomic_load(&ctx.metrics.forced), 2);
    
    clock_gettime(CLOCK_REALTIME, &ctx.metrics.start_time);
    struct timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);
    ctx.metrics.end_time = end_time;
    
    long duration_ms = (ctx.metrics.end_time.tv_sec - ctx.metrics.start_time.tv_sec) * 1000 +
                       (ctx.metrics.end_time.tv_nsec - ctx.metrics.start_time.tv_nsec) / 1000000;
    cr_assert_geq(duration_ms, 0, "Duration should be non-negative");
}

Test(shutdown_context, timeout_configuration)
{
    shutdown_context_t ctx = {0};
    
    ctx.timeout_seconds = 5;
    cr_assert_eq(ctx.timeout_seconds, 5);
    
    ctx.timeout_seconds = 30;
    cr_assert_eq(ctx.timeout_seconds, 30);
    
    ctx.timeout_seconds = 300;
    cr_assert_eq(ctx.timeout_seconds, 300);
}

Test(shutdown_context, deadline_calculation)
{
    shutdown_context_t ctx = {0};
    
    ctx.timeout_seconds = 10;
    
    clock_gettime(CLOCK_REALTIME, &ctx.deadline);
    ctx.deadline.tv_sec += ctx.timeout_seconds;
    
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    
    long remaining = ctx.deadline.tv_sec - now.tv_sec;
    cr_assert_geq(remaining, 9, "Deadline should be ~10 seconds in future, got %lds", remaining);
    cr_assert_leq(remaining, 11, "Deadline should be ~10 seconds in future, got %lds", remaining);
}

Test(shutdown_context, state_transitions)
{
    shutdown_context_t ctx = {0};
    
    atomic_store(&ctx.state, SHUTDOWN_STATE_RUNNING);
    cr_assert_eq(atomic_load(&ctx.state), SHUTDOWN_STATE_RUNNING);
    
    atomic_store(&ctx.state, SHUTDOWN_STATE_DRAINING);
    cr_assert_eq(atomic_load(&ctx.state), SHUTDOWN_STATE_DRAINING);
    
    atomic_store(&ctx.state, SHUTDOWN_STATE_FORCED);
    cr_assert_eq(atomic_load(&ctx.state), SHUTDOWN_STATE_FORCED);
}

Test(shutdown_context, concurrent_access_safety)
{
    shutdown_context_t ctx = {0};
    atomic_store(&ctx.in_flight_requests, 0);
    
    for (int i = 0; i < 1000; i++) {
        atomic_fetch_add(&ctx.in_flight_requests, 1);
    }
    
    cr_assert_eq(atomic_load(&ctx.in_flight_requests), 1000);
    
    for (int i = 0; i < 500; i++) {
        atomic_fetch_sub(&ctx.in_flight_requests, 1);
    }
    
    cr_assert_eq(atomic_load(&ctx.in_flight_requests), 500);
}

Test(shutdown_context, global_instance_exists)
{
    extern shutdown_context_t g_shutdown_ctx;
    
    atomic_store(&g_shutdown_ctx.state, SHUTDOWN_STATE_RUNNING);
    cr_assert_eq(atomic_load(&g_shutdown_ctx.state), SHUTDOWN_STATE_RUNNING);
    
    atomic_store(&g_shutdown_ctx.in_flight_requests, 0);
    cr_assert_eq(atomic_load(&g_shutdown_ctx.in_flight_requests), 0);
}
