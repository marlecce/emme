#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>
#include <stdbool.h>
#include <config.h>

typedef struct {
    void (*function)(void *);
    void *arg;
} Task;

typedef struct {
    int client_fd;
    ServerConfig *config;
} ClientTaskData;

// Opaque thread pool type.
typedef struct ThreadPool ThreadPool;

// Creates a thread pool with at least 'min_threads' and at most 'max_threads' worker threads.
ThreadPool *thread_pool_create(size_t min_threads, size_t max_threads);

// Adds a task to the thread pool; returns true if the task was added.
bool thread_pool_add_task(ThreadPool *pool, void (*function)(void *), void *arg);

// Destroys the thread pool and cleans up resources.
void thread_pool_destroy(ThreadPool *pool);

#endif // THREAD_POOL_H
