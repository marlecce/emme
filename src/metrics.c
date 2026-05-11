#include "metrics.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define METRICS_BUFFER_SIZE 65536
#define METRICS_REQUEST_BUFFER_SIZE 4096
#define METRICS_RESPONSE_BUFFER_SIZE 8192
#define METRICS_SERVER_BACKLOG 10

static MetricsRegistry g_metrics;
static int g_metrics_server_fd = -1;
static pthread_t g_metrics_server_thread;
static _Atomic int g_server_running = 0;

static const double HISTOGRAM_BUCKET_BOUNDS[HISTOGRAM_BUCKETS] = {
    0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 25.0, 50.0
};

static void initialize_histogram(MetricHistogram *histogram, const char *name,
                                  const char *help)
{
    histogram->metadata.name = name;
    histogram->metadata.help = help;
    histogram->metadata.type = "histogram";
    for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
        histogram->data.buckets[i] = HISTOGRAM_BUCKET_BOUNDS[i];
    }
    pthread_mutex_init(&histogram->data.lock, NULL);
}

void metrics_init(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    
    g_metrics.requests_total.metadata.name = "emme_requests_total";
    g_metrics.requests_total.metadata.help = "Total number of HTTP requests";
    g_metrics.requests_total.metadata.type = "counter";
    
    g_metrics.tls_handshakes_total.metadata.name = "emme_tls_handshakes_total";
    g_metrics.tls_handshakes_total.metadata.help = "Total number of TLS handshakes";
    g_metrics.tls_handshakes_total.metadata.type = "counter";
    
    g_metrics.request_timeouts_total.metadata.name = "emme_request_timeouts_total";
    g_metrics.request_timeouts_total.metadata.help = "Total number of request timeouts";
    g_metrics.request_timeouts_total.metadata.type = "counter";
    
    g_metrics.active_connections.metadata.name = "emme_active_connections";
    g_metrics.active_connections.metadata.help = "Number of active connections";
    g_metrics.active_connections.metadata.type = "gauge";
    
    g_metrics.thread_pool_active_threads.metadata.name = "emme_thread_pool_active_threads";
    g_metrics.thread_pool_active_threads.metadata.help = "Number of active threads in the pool";
    g_metrics.thread_pool_active_threads.metadata.type = "gauge";
    
    g_metrics.thread_pool_queue_depth.metadata.name = "emme_thread_pool_queue_depth";
    g_metrics.thread_pool_queue_depth.metadata.help = "Depth of the thread pool queue";
    g_metrics.thread_pool_queue_depth.metadata.type = "gauge";
    
    g_metrics.shutdown_drain_active.metadata.name = "emme_shutdown_drain_active";
    g_metrics.shutdown_drain_active.metadata.help = "1 if shutdown drain is active, 0 otherwise";
    g_metrics.shutdown_drain_active.metadata.type = "gauge";
    
    initialize_histogram(&g_metrics.request_duration_seconds,
                         "emme_request_duration_seconds",
                         "HTTP request duration in seconds");
    
    initialize_histogram(&g_metrics.tls_handshake_duration_seconds,
                         "emme_tls_handshake_duration_seconds",
                         "TLS handshake duration in seconds");
    
    g_metrics.io_uring_sqe_depth.metadata.name = "emme_io_uring_sqe_depth";
    g_metrics.io_uring_sqe_depth.metadata.help = "io_uring submission queue depth";
    g_metrics.io_uring_sqe_depth.metadata.type = "gauge";
    
    g_metrics.io_uring_cqe_depth.metadata.name = "emme_io_uring_cqe_depth";
    g_metrics.io_uring_cqe_depth.metadata.help = "io_uring completion queue depth";
    g_metrics.io_uring_cqe_depth.metadata.type = "gauge";
    
    g_metrics.backend_pool_active.metadata.name = "emme_backend_pool_active_connections";
    g_metrics.backend_pool_active.metadata.help = "Number of active backend connections";
    g_metrics.backend_pool_active.metadata.type = "gauge";
    
    g_metrics.backend_pool_idle.metadata.name = "emme_backend_pool_idle_connections";
    g_metrics.backend_pool_idle.metadata.help = "Number of idle backend connections";
    g_metrics.backend_pool_idle.metadata.type = "gauge";
    
    g_metrics.backend_pool_healthy.metadata.name = "emme_backend_pool_healthy_connections";
    g_metrics.backend_pool_healthy.metadata.help = "Number of healthy backend connections";
    g_metrics.backend_pool_healthy.metadata.type = "gauge";
    
    g_metrics.health_checker_total_checks.metadata.name = "emme_health_checker_total_checks";
    g_metrics.health_checker_total_checks.metadata.help = "Total health checks performed";
    g_metrics.health_checker_total_checks.metadata.type = "gauge";
    
    g_metrics.health_checker_failed_checks.metadata.name = "emme_health_checker_failed_checks";
    g_metrics.health_checker_failed_checks.metadata.help = "Total failed health checks";
    g_metrics.health_checker_failed_checks.metadata.type = "gauge";
    
    g_metrics.health_checker_health_status.metadata.name = "emme_health_checker_health_status";
    g_metrics.health_checker_health_status.metadata.help = "Backend health status (0=unknown, 1=healthy, 2=unhealthy)";
    g_metrics.health_checker_health_status.metadata.type = "gauge";
    
    g_metrics.health_checker_last_check_time.metadata.name = "emme_health_checker_last_check_time_seconds";
    g_metrics.health_checker_last_check_time.metadata.help = "Unix timestamp of last health check";
    g_metrics.health_checker_last_check_time.metadata.type = "gauge";
    
    g_metrics.circuit_breaker_state.metadata.name = "emme_circuit_breaker_state";
    g_metrics.circuit_breaker_state.metadata.help = "Circuit breaker state (0=closed, 1=open, 2=half-open)";
    g_metrics.circuit_breaker_state.metadata.type = "gauge";
    
    g_metrics.circuit_breaker_failure_count.metadata.name = "emme_circuit_breaker_failure_count";
    g_metrics.circuit_breaker_failure_count.metadata.help = "Current failure count";
    g_metrics.circuit_breaker_failure_count.metadata.type = "gauge";
    
    g_metrics.circuit_breaker_total_opens.metadata.name = "emme_circuit_breaker_total_opens";
    g_metrics.circuit_breaker_total_opens.metadata.help = "Total times circuit breaker opened";
    g_metrics.circuit_breaker_total_opens.metadata.type = "gauge";
    
    g_metrics.circuit_breaker_total_closes.metadata.name = "emme_circuit_breaker_total_closes";
    g_metrics.circuit_breaker_total_closes.metadata.help = "Total times circuit breaker closed";
    g_metrics.circuit_breaker_total_closes.metadata.type = "gauge";
    
    log_message(LOG_LEVEL_INFO, "Metrics registry initialized");
}

void metrics_shutdown(void)
{
    metrics_stop_server();
    pthread_mutex_destroy(&g_metrics.request_duration_seconds.data.lock);
    pthread_mutex_destroy(&g_metrics.tls_handshake_duration_seconds.data.lock);
    log_message(LOG_LEVEL_INFO, "Metrics registry shutdown complete");
}

static void histogram_record(HistogramData *histogram, double value)
{
    pthread_mutex_lock(&histogram->lock);
    
    for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
        if (value <= histogram->buckets[i]) {
            atomic_fetch_add(&histogram->counts[i], 1);
            break;
        }
    }
    
    atomic_fetch_add(&histogram->sum_count, 1);
    histogram->sum_total += value;
    
    pthread_mutex_unlock(&histogram->lock);
}

void metrics_increment_request(const char *method, const char *path, int status)
{
    (void)method;
    (void)path;
    (void)status;
    atomic_fetch_add(&g_metrics.requests_total.value, 1);
}

void metrics_record_request_duration(double duration_seconds)
{
    histogram_record(&g_metrics.request_duration_seconds.data, duration_seconds);
}

void metrics_increment_request_timeouts(void)
{
    atomic_fetch_add(&g_metrics.request_timeouts_total.value, 1);
}

void metrics_set_active_connections(long count)
{
    atomic_store(&g_metrics.active_connections.value, count);
}

void metrics_set_thread_pool_stats(int active_threads, int queue_depth)
{
    atomic_store(&g_metrics.thread_pool_active_threads.value, active_threads);
    atomic_store(&g_metrics.thread_pool_queue_depth.value, queue_depth);
}

void metrics_increment_tls_handshake(int success)
{
    (void)success;
    atomic_fetch_add(&g_metrics.tls_handshakes_total.value, 1);
}

void metrics_record_tls_handshake_duration(double duration_seconds)
{
    histogram_record(&g_metrics.tls_handshake_duration_seconds.data, duration_seconds);
}

void metrics_set_io_uring_depth(int sqe_depth, int cqe_depth)
{
    atomic_store(&g_metrics.io_uring_sqe_depth.value, sqe_depth);
    atomic_store(&g_metrics.io_uring_cqe_depth.value, cqe_depth);
}

void metrics_set_shutdown_drain(int active)
{
    atomic_store(&g_metrics.shutdown_drain_active.value, active ? 1 : 0);
}

void metrics_set_backend_pool_stats(const char *backend, int active, int idle, int healthy)
{
    (void)backend;
    atomic_store(&g_metrics.backend_pool_active.value, active);
    atomic_store(&g_metrics.backend_pool_idle.value, idle);
    atomic_store(&g_metrics.backend_pool_healthy.value, healthy);
}

void metrics_set_health_checker_stats(const char *backend, int total_checks, int failed_checks,
                                       int health_status, long last_check_time)
{
    (void)backend;
    atomic_store(&g_metrics.health_checker_total_checks.value, total_checks);
    atomic_store(&g_metrics.health_checker_failed_checks.value, failed_checks);
    atomic_store(&g_metrics.health_checker_health_status.value, health_status);
    atomic_store(&g_metrics.health_checker_last_check_time.value, last_check_time);
}

void metrics_set_circuit_breaker_stats(const char *backend, int state, int failure_count,
                                        long total_opens, long total_closes)
{
    (void)backend;
    atomic_store(&g_metrics.circuit_breaker_state.value, state);
    atomic_store(&g_metrics.circuit_breaker_failure_count.value, failure_count);
    atomic_store(&g_metrics.circuit_breaker_total_opens.value, total_opens);
    atomic_store(&g_metrics.circuit_breaker_total_closes.value, total_closes);
}

static int format_counter(char *buffer, size_t remaining, const MetricCounter *counter)
{
    return snprintf(buffer, remaining,
                   "# HELP %s %s\n# TYPE %s counter\n%ld\n",
                   counter->metadata.name,
                   counter->metadata.help,
                   counter->metadata.type,
                   atomic_load(&counter->value));
}

static int format_gauge(char *buffer, size_t remaining, const MetricGauge *gauge)
{
    return snprintf(buffer, remaining,
                   "# HELP %s %s\n# TYPE %s gauge\n%ld\n",
                   gauge->metadata.name,
                   gauge->metadata.help,
                   gauge->metadata.type,
                   atomic_load(&gauge->value));
}

static int format_histogram(char *buffer, size_t remaining, const MetricHistogram *histogram)
{
    int written = 0;
    size_t offset = 0;
    
    written = snprintf(buffer, remaining,
                      "# HELP %s %s\n# TYPE histogram\n",
                      histogram->metadata.name,
                      histogram->metadata.help);
    if (written < 0 || (size_t)written >= remaining) {
        return -1;
    }
    offset = (size_t)written;
    
    pthread_mutex_lock((pthread_mutex_t *)&histogram->data.lock);
    long cumulative = 0;
    
    for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
        cumulative += atomic_load(&histogram->data.counts[i]);
        int n = snprintf(buffer + offset, remaining - offset,
                        "%s_bucket{le=\"%.3f\"} %ld\n",
                        histogram->metadata.name,
                        histogram->data.buckets[i],
                        cumulative);
        if (n < 0 || (size_t)n >= remaining - offset) {
            pthread_mutex_unlock((pthread_mutex_t *)&histogram->data.lock);
            return -1;
        }
        offset += (size_t)n;
    }
    
    int n = snprintf(buffer + offset, remaining - offset,
                    "%s_bucket{le=\"+Inf\"} %ld\n",
                    histogram->metadata.name,
                    atomic_load(&histogram->data.sum_count));
    if (n < 0 || (size_t)n >= remaining - offset) {
        pthread_mutex_unlock((pthread_mutex_t *)&histogram->data.lock);
        return -1;
    }
    offset += (size_t)n;
    
    n = snprintf(buffer + offset, remaining - offset,
                "%s_sum %f\n",
                histogram->metadata.name,
                histogram->data.sum_total);
    if (n < 0 || (size_t)n >= remaining - offset) {
        pthread_mutex_unlock((pthread_mutex_t *)&histogram->data.lock);
        return -1;
    }
    offset += (size_t)n;
    
    n = snprintf(buffer + offset, remaining - offset,
                "%s_count %ld\n",
                histogram->metadata.name,
                atomic_load(&histogram->data.sum_count));
    if (n < 0 || (size_t)n >= remaining - offset) {
        pthread_mutex_unlock((pthread_mutex_t *)&histogram->data.lock);
        return -1;
    }
    offset += (size_t)n;
    
    pthread_mutex_unlock((pthread_mutex_t *)&histogram->data.lock);
    return (int)offset;
}

char *metrics_format_prometheus(void)
{
    char *buffer = malloc(METRICS_BUFFER_SIZE);
    if (!buffer) {
        return NULL;
    }
    
    size_t offset = 0;
    int written = 0;
    
    written = format_counter(buffer + offset, METRICS_BUFFER_SIZE - offset,
                            &g_metrics.requests_total);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_counter(buffer + offset, METRICS_BUFFER_SIZE - offset,
                            &g_metrics.tls_handshakes_total);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_counter(buffer + offset, METRICS_BUFFER_SIZE - offset,
                            &g_metrics.request_timeouts_total);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.active_connections);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.thread_pool_active_threads);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.thread_pool_queue_depth);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.shutdown_drain_active);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_histogram(buffer + offset, METRICS_BUFFER_SIZE - offset,
                              &g_metrics.request_duration_seconds);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_histogram(buffer + offset, METRICS_BUFFER_SIZE - offset,
                              &g_metrics.tls_handshake_duration_seconds);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.io_uring_sqe_depth);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.io_uring_cqe_depth);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.backend_pool_active);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.backend_pool_idle);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.backend_pool_healthy);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.health_checker_total_checks);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.health_checker_failed_checks);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.health_checker_health_status);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.health_checker_last_check_time);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.circuit_breaker_state);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.circuit_breaker_failure_count);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.circuit_breaker_total_opens);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    written = format_gauge(buffer + offset, METRICS_BUFFER_SIZE - offset,
                          &g_metrics.circuit_breaker_total_closes);
    if (written < 0) goto truncation_error;
    offset += (size_t)written;
    
    return buffer;

truncation_error:
    free(buffer);
    return NULL;
}

void metrics_free_format(char *formatted)
{
    if (formatted) {
        free(formatted);
    }
}

static void *metrics_server_thread(void *arg)
{
    (void)arg;
    int server_fd = g_metrics_server_fd;
    
    while (atomic_load(&g_server_running)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (atomic_load(&g_server_running)) {
                log_message(LOG_LEVEL_ERROR, "Metrics server accept failed");
            }
            continue;
        }
        
        char request_buffer[METRICS_REQUEST_BUFFER_SIZE];
        ssize_t bytes_read = read(client_fd, request_buffer, sizeof(request_buffer) - 1);
        if (bytes_read > 0) {
            request_buffer[bytes_read] = '\0';
            
            if (strstr(request_buffer, "GET /metrics") != NULL) {
                char *metrics_data = metrics_format_prometheus();
                if (metrics_data) {
                    char response[METRICS_RESPONSE_BUFFER_SIZE];
                    size_t metrics_len = strlen(metrics_data);
                    int response_len = snprintf(response, sizeof(response),
                                              "HTTP/1.1 200 OK\r\n"
                                              "Content-Type: text/plain; version=0.0.4\r\n"
                                              "Content-Length: %zu\r\n"
                                              "Connection: close\r\n"
                                              "\r\n"
                                              "%s",
                                              metrics_len, metrics_data);
                    
                    if (response_len > 0 && response_len < (int)sizeof(response)) {
                        write(client_fd, response, (size_t)response_len);
                    }
                    metrics_free_format(metrics_data);
                }
            } else {
                const char *response = "HTTP/1.1 404 Not Found\r\n\r\n";
                write(client_fd, response, strlen(response));
            }
        }
        
        close(client_fd);
    }
    
    return NULL;
}

int metrics_start_server(int port)
{
    g_metrics_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_metrics_server_fd < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create metrics socket");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(g_metrics_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to set SO_REUSEADDR on metrics socket");
        close(g_metrics_server_fd);
        return -1;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    
    if (bind(g_metrics_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to bind metrics server to port %d", port);
        close(g_metrics_server_fd);
        return -1;
    }
    
    if (listen(g_metrics_server_fd, METRICS_SERVER_BACKLOG) < 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to listen on metrics socket");
        close(g_metrics_server_fd);
        return -1;
    }
    
    atomic_store(&g_server_running, 1);
    
    if (pthread_create(&g_metrics_server_thread, NULL, metrics_server_thread, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to create metrics server thread");
        atomic_store(&g_server_running, 0);
        close(g_metrics_server_fd);
        return -1;
    }
    
    log_message(LOG_LEVEL_INFO, "Metrics server started on http://127.0.0.1:%d/metrics", port);
    return 0;
}

void metrics_stop_server(void)
{
    if (atomic_load(&g_server_running)) {
        atomic_store(&g_server_running, 0);
        
        if (g_metrics_server_fd >= 0) {
            shutdown(g_metrics_server_fd, SHUT_RDWR);
            close(g_metrics_server_fd);
            g_metrics_server_fd = -1;
        }
        
        pthread_join(g_metrics_server_thread, NULL);
        log_message(LOG_LEVEL_INFO, "Metrics server stopped");
    }
}
