#ifndef METRICS_H
#define METRICS_H

#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#define MAX_LABELS 4
#define MAX_LABEL_KEY_LEN 32
#define MAX_LABEL_VALUE_LEN 64
#define HISTOGRAM_BUCKETS 14

typedef struct {
    char key[MAX_LABEL_KEY_LEN];
    char value[MAX_LABEL_VALUE_LEN];
} MetricLabel;

typedef struct {
    const char *name;
    const char *help;
    const char *type;
    MetricLabel labels[MAX_LABELS];
    int label_count;
} MetricMetadata;

typedef struct {
    MetricMetadata metadata;
    _Atomic long value;
} MetricCounter;

typedef struct {
    MetricMetadata metadata;
    _Atomic long value;
} MetricGauge;

typedef struct {
    double buckets[HISTOGRAM_BUCKETS];
    _Atomic long counts[HISTOGRAM_BUCKETS];
    _Atomic long sum_count;
    double sum_total;
    pthread_mutex_t lock;
} HistogramData;

typedef struct {
    MetricMetadata metadata;
    HistogramData data;
} MetricHistogram;

typedef struct {
    MetricCounter requests_total;
    MetricCounter tls_handshakes_total;
    MetricGauge active_connections;
    MetricGauge thread_pool_active_threads;
    MetricGauge thread_pool_queue_depth;
    MetricGauge shutdown_drain_active;
    MetricHistogram request_duration_seconds;
    MetricHistogram tls_handshake_duration_seconds;
    MetricGauge io_uring_sqe_depth;
    MetricGauge io_uring_cqe_depth;
    MetricGauge backend_pool_active;
    MetricGauge backend_pool_idle;
    MetricGauge backend_pool_healthy;
    MetricGauge health_checker_total_checks;
    MetricGauge health_checker_failed_checks;
    MetricGauge health_checker_health_status;
    MetricGauge health_checker_last_check_time;
    MetricGauge circuit_breaker_state;
    MetricGauge circuit_breaker_failure_count;
    MetricGauge circuit_breaker_total_opens;
    MetricGauge circuit_breaker_total_closes;
} MetricsRegistry;

void metrics_init(void);
void metrics_shutdown(void);
int metrics_start_server(int port);
void metrics_stop_server(void);

void metrics_increment_request(const char *method, const char *path, int status);
void metrics_record_request_duration(double duration_seconds);
void metrics_set_active_connections(long count);
void metrics_set_thread_pool_stats(int active_threads, int queue_depth);
void metrics_increment_tls_handshake(int success);
void metrics_record_tls_handshake_duration(double duration_seconds);
void metrics_set_io_uring_depth(int sqe_depth, int cqe_depth);
void metrics_set_shutdown_drain(int active);
void metrics_set_backend_pool_stats(const char *backend, int active, int idle, int healthy);
void metrics_set_health_checker_stats(const char *backend, int total_checks, int failed_checks, 
                                       int health_status, long last_check_time);
void metrics_set_circuit_breaker_stats(const char *backend, int state, int failure_count,
                                        long total_opens, long total_closes);

char *metrics_format_prometheus(void);
void metrics_free_format(char *formatted);

#endif
