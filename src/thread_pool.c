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

// The ThreadPool structure.
struct ThreadPool {
    pthread_mutex_t lock;   // Mutex to protect the pool and queue.
    pthread_cond_t cond;    // Condition variable to signal new tasks.
    TaskQueue queue;        // Task queue.
    pthread_t *threads;     // Array of worker thread IDs.
    size_t num_threads;     // Current number of worker threads.
    size_t min_threads;     // Minimum number of worker threads.
    size_t max_threads;     // Maximum number of worker threads.
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

// Worker thread function. Each thread waits for tasks and executes them.
static void *worker_thread(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
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
                    pool->num_threads--;
                    pthread_mutex_unlock(&pool->lock);
                    pthread_exit(NULL);
                }
            }
        }
        // If shutdown has been signaled, exit the thread.
        if (pool->shutdown) {
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
    pool->shutdown = false;
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    task_queue_init(&pool->queue);
    pool->threads = malloc(max_threads * sizeof(pthread_t));
    if (!pool->threads) {
        free(pool);
        return NULL;
    }
    // Create the initial minimum number of worker threads.
    for (size_t i = 0; i < min_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
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

    // Pre-allocazione della memoria per evitare allocazioni durante l'esecuzione
    if (pool->queue.count >= pool->queue.capacity * 0.8) {
        size_t new_capacity = pool->queue.capacity * 2;
        CacheAlignedTask *new_tasks = realloc(pool->queue.tasks, 
                                             new_capacity * sizeof(CacheAlignedTask));
        if (new_tasks) {
            pool->queue.tasks = new_tasks;
            pool->queue.capacity = new_capacity;
        }
    }

    Task task = {function, arg};
    if (!task_queue_push(&pool->queue, task)) {
        pthread_mutex_unlock(&pool->lock);
        return false;
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
    // Join all threads. We use the maximum number as an upper bound.
    for (size_t i = 0; i < pool->max_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    free(pool->threads);
    task_queue_destroy(&pool->queue);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    free(pool);
}
