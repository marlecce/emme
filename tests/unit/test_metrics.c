#include <criterion/criterion.h>
#include <criterion/logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "metrics.h"

Test(metrics, init_and_shutdown)
{
    metrics_init();
    cr_assert(1, "Metrics init should succeed");
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted, "Should format metrics");
    cr_assert(strstr(formatted, "emme_requests_total") != NULL, "Should contain requests metric");
    cr_assert(strstr(formatted, "emme_active_connections") != NULL, "Should contain connections metric");
    metrics_free_format(formatted);
    
    metrics_shutdown();
}

Test(metrics, counter_operations)
{
    metrics_init();
    
    metrics_increment_request("GET", "/test", 200);
    metrics_increment_request("GET", "/test", 200);
    metrics_increment_request("POST", "/api", 201);
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    cr_assert(strstr(formatted, "emme_requests_total") != NULL);
    metrics_free_format(formatted);
    
    metrics_shutdown();
}

Test(metrics, gauge_set)
{
    metrics_init();
    
    metrics_set_active_connections(10);
    metrics_set_thread_pool_stats(5, 3);
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    cr_assert(strstr(formatted, "emme_active_connections") != NULL);
    cr_assert(strstr(formatted, "emme_thread_pool_active_threads") != NULL);
    cr_assert(strstr(formatted, "emme_thread_pool_queue_depth") != NULL);
    metrics_free_format(formatted);
    
    metrics_shutdown();
}

Test(metrics, histogram_record)
{
    metrics_init();
    
    metrics_record_request_duration(0.005);
    metrics_record_request_duration(0.015);
    metrics_record_request_duration(0.1);
    metrics_record_request_duration(1.5);
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    cr_assert(strstr(formatted, "emme_request_duration_seconds_bucket") != NULL);
    cr_assert(strstr(formatted, "emme_request_duration_seconds_sum") != NULL);
    cr_assert(strstr(formatted, "emme_request_duration_seconds_count") != NULL);
    metrics_free_format(formatted);
    
    metrics_shutdown();
}

Test(metrics, tls_handshake_tracking)
{
    metrics_init();
    
    metrics_increment_tls_handshake(1);
    metrics_increment_tls_handshake(1);
    metrics_increment_tls_handshake(0);
    metrics_record_tls_handshake_duration(0.005);
    metrics_record_tls_handshake_duration(0.010);
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    cr_assert(strstr(formatted, "emme_tls_handshakes_total") != NULL);
    cr_assert(strstr(formatted, "emme_tls_handshake_duration_seconds") != NULL);
    metrics_free_format(formatted);
    
    metrics_shutdown();
}

Test(metrics, shutdown_drain_tracking)
{
    metrics_init();
    
    metrics_set_shutdown_drain(1);
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    cr_assert(strstr(formatted, "emme_shutdown_drain_active") != NULL, "Should contain metric name");
    cr_assert(strstr(formatted, "\n1\n") != NULL, "Should show value 1");
    metrics_free_format(formatted);
    
    metrics_set_shutdown_drain(0);
    formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    cr_assert(strstr(formatted, "emme_shutdown_drain_active") != NULL, "Should contain metric name");
    metrics_free_format(formatted);
    
    metrics_shutdown();
}

Test(metrics, format_prometheus_valid)
{
    metrics_init();
    
    metrics_increment_request("GET", "/", 200);
    metrics_set_active_connections(5);
    metrics_record_request_duration(0.05);
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    
    cr_assert(strstr(formatted, "# HELP") != NULL, "Should contain HELP");
    cr_assert(strstr(formatted, "# TYPE") != NULL, "Should contain TYPE");
    cr_assert(strstr(formatted, "counter") != NULL, "Should contain counter type");
    cr_assert(strstr(formatted, "gauge") != NULL, "Should contain gauge type");
    cr_assert(strstr(formatted, "histogram") != NULL, "Should contain histogram type");
    
    metrics_free_format(formatted);
    metrics_shutdown();
}

Test(metrics, multiple_operations)
{
    metrics_init();
    
    for (int i = 0; i < 100; i++) {
        metrics_increment_request("GET", "/test", 200);
        metrics_set_active_connections(i % 10);
        metrics_record_request_duration(0.001 * i);
    }
    
    char *formatted = metrics_format_prometheus();
    cr_assert_not_null(formatted);
    
    char *requests_line = strstr(formatted, "emme_requests_total");
    cr_assert_not_null(requests_line);
    cr_assert(strstr(requests_line, "100") != NULL, "Should have 100 requests");
    
    metrics_free_format(formatted);
    metrics_shutdown();
}
