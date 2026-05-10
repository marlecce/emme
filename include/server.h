#ifndef SERVER_H
#define SERVER_H

#include "config.h"
#include <liburing.h>
#include <stdatomic.h>
#include <time.h>

#define BUFFER_SIZE 32768
#define QUEUE_DEPTH 64

typedef enum {
    SHUTDOWN_STATE_RUNNING = 0,
    SHUTDOWN_STATE_DRAINING = 1,
    SHUTDOWN_STATE_FORCED = 2
} shutdown_state_t;

typedef struct {
    _Atomic shutdown_state_t state;
    _Atomic size_t in_flight_requests;
    struct timespec deadline;
    struct {
        _Atomic size_t completed;
        _Atomic size_t forced;
        _Atomic size_t peak_in_flight;
        struct timespec start_time;
        struct timespec end_time;
    } metrics;
    int timeout_seconds;
} shutdown_context_t;

extern shutdown_context_t g_shutdown_ctx;

void handle_client(int client_fd, ServerConfig *config, struct io_uring *ring);
int start_server(ServerConfig *config);

#endif
