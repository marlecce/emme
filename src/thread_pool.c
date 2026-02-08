#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#define TASK_QUEUE_INITIAL_CAPACITY 256
#define THREAD_IDLE_TIMEOUT 5 // Idle timeout in seconds

typedef struct {
    Task task;
    char padding[64 - sizeof(Task)];
} CacheAlignedTask;

typedef struct {
    CacheAlignedTask *tasks;
    size_t capacity;
    volatile size_t count;
    volatile size_t front;
    volatile size_t rear;
    char padding[64];
} TaskQueue;

typedef struct {
    struct ThreadPool *pool;
    size_t index;
} WorkerArg;

// The ThreadPool structure.
struct ThreadPool {
    pthread_mutex_t lock;   // Mutex to protect the pool and queue.
    pthread_cond_t cond;    // Condition variable to signal new tasks.
    TaskQueue queue;        // Task queue.
    pthread_t *threads;     // Array of worker thread IDs.
    WorkerArg *worker_args; // Worker thread arguments.
    size_t num_threads;     // Current number of worker threads.
    size_t threads_created; // Total threads created (upper bound for joins).
    size_t min_threads;     // Minimum number of worker threads.
    size_t max_threads;     // Maximum number of worker threads.
    size_t *free_indices;   // Stack of reusable thread slots.
    size_t free_count;      // Number of reusable slots.
    bool shutdown;          // Flag to signal shutdown.
};

// Initializes the task queue.
static void task_queue_init(TaskQueue *queue) {
    queue->capacity = TASK_QUEUE_INITIAL_CAPACITY;
    queue->tasks = malloc(queue->capacity * sizeof(CacheAlignedTask));
    queue->count = 0;
    queue->front = 0;
    queue->rear = 0;
}

// Destroys the task queue.
static void task_queue_destroy(TaskQueue *queue) {
    free(queue->tasks);
}

// Pushes a task onto the task queue; returns true on success.
static bool task_queue_push(TaskQueue *queue, Task task) {
    if (queue->count == queue->capacity) {
        size_t new_capacity = queue->capacity * 2;
        CacheAlignedTask *new_tasks = malloc(new_capacity * sizeof(CacheAlignedTask));
        if (!new_tasks) return false;

        if (queue->front > 0) {
            for (size_t i = 0; i < queue->count; i++) {
                new_tasks[i].task = queue->tasks[(queue->front + i) % queue->capacity].task;
            }
            queue->front = 0;
            queue->rear = queue->count;
        }
        free(queue->tasks);
        queue->tasks = new_tasks;
        queue->capacity = new_capacity;
    }

    queue->tasks[queue->rear].task = task;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->count++;
    return true;
}

// Pops a task from the task queue; returns true if a task was retrieved.
static bool task_queue_pop(TaskQueue *queue, Task *task) {
    if (queue->count == 0) return false;
    
    *task = queue->tasks[queue->front].task;
    queue->front = (queue->front + 1) % queue->capacity;
    queue->count--;
    return true;
}

static void mark_thread_exit(ThreadPool *pool, size_t index) {
    if (pool->free_count < pool->max_threads) {
        pool->free_indices[pool->free_count++] = index;
    }
    if (pool->num_threads > 0) {
        pool->num_threads--;
    }
}

// Worker thread function. Each thread waits for tasks and executes them.
static void *worker_thread(void *arg) {
    WorkerArg *warg = (WorkerArg *)arg;
    ThreadPool *pool = warg->pool;
    size_t index = warg->index;
    Task task;
    while (1) {
        pthread_mutex_lock(&pool->lock);
        // Wait for a task if the queue is empty.
        while (pool->queue.count == 0 && !pool->shutdown) {
            // Wait with a timeout to allow dynamic thread reduction.
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += THREAD_IDLE_TIMEOUT;
            int ret = pthread_cond_timedwait(&pool->cond, &pool->lock, &ts);
            // If timed out and still no task, consider exiting.
            if (ret == ETIMEDOUT && pool->queue.count == 0) {
                if (pool->num_threads > pool->min_threads) {
                    mark_thread_exit(pool, index);
                    pthread_mutex_unlock(&pool->lock);
                    pthread_exit(NULL);
                }
            }
        }
        // If shutdown has been signaled, exit the thread.
        if (pool->shutdown) {
            mark_thread_exit(pool, index);
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }
        // Retrieve the next task from the queue.
        bool has_task = task_queue_pop(&pool->queue, &task);
        pthread_mutex_unlock(&pool->lock);
        if (has_task) {
            // Execute the task.
            task.function(task.arg);
        }
    }
    return NULL;
}

// Creates a new thread pool with dynamic resizing capabilities.
ThreadPool *thread_pool_create(size_t min_threads, size_t max_threads) {
    ThreadPool *pool = malloc(sizeof(ThreadPool));
    if (!pool)
        return NULL;
    pool->min_threads = min_threads;
    pool->max_threads = max_threads;
    pool->num_threads = min_threads;
    pool->threads_created = 0;
    pool->shutdown = false;
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    task_queue_init(&pool->queue);
    pool->threads = malloc(max_threads * sizeof(pthread_t));
    pool->worker_args = malloc(max_threads * sizeof(WorkerArg));
    pool->free_indices = malloc(max_threads * sizeof(size_t));
    pool->free_count = 0;
    if (!pool->threads || !pool->worker_args || !pool->free_indices) {
        free(pool->threads);
        free(pool->worker_args);
        free(pool->free_indices);
        free(pool);
        return NULL;
    }
    // Create the initial minimum number of worker threads.
    for (size_t i = 0; i < min_threads; i++) {
        pool->worker_args[i].pool = pool;
        pool->worker_args[i].index = i;
        pthread_create(&pool->threads[i], NULL, worker_thread, &pool->worker_args[i]);
        pool->threads_created++;
    }
    return pool;
}

// Adds a task to the thread pool. If the task queue length exceeds the current number
// of threads and we have not reached max_threads, a new thread is spawned.
bool thread_pool_add_task(ThreadPool *pool, void (*function)(void *), void *arg) {
    pthread_mutex_lock(&pool->lock);
    
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        return false;
    }

    Task task = {function, arg};
    if (!task_queue_push(&pool->queue, task)) {
        pthread_mutex_unlock(&pool->lock);
        return false;
    }

    if (pool->queue.count > pool->num_threads && pool->num_threads < pool->max_threads) {
        size_t index;
        if (pool->free_count > 0) {
            index = pool->free_indices[--pool->free_count];
        } else {
            if (pool->threads_created >= pool->max_threads) {
                index = pool->max_threads;
            } else {
                index = pool->threads_created++;
            }
        }
        if (index < pool->max_threads) {
            pool->worker_args[index].pool = pool;
            pool->worker_args[index].index = index;
            if (pthread_create(&pool->threads[index], NULL, worker_thread, &pool->worker_args[index]) == 0) {
                pool->num_threads++;
            }
        }
    }

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return true;
}

// Destroys the thread pool by signaling shutdown and joining all threads.
void thread_pool_destroy(ThreadPool *pool) {
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    // Join all threads that were created.
    for (size_t i = 0; i < pool->threads_created; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    free(pool->threads);
    free(pool->worker_args);
    free(pool->free_indices);
    task_queue_destroy(&pool->queue);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    free(pool);
}
