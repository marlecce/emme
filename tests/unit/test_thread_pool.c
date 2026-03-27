// tests/unit/test_thread_pool.c

#include <criterion/criterion.h>

#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "thread_pool.h"

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int completed;
    int target;
} CountSync;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int started;
    int completed;
    int release;
    int total;
} BlockSync;

static void timespec_add_ms(struct timespec *ts, int ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

static int wait_for_value(pthread_cond_t *cond, pthread_mutex_t *lock, int *value,
                          int target, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    timespec_add_ms(&ts, timeout_ms);
    while (*value < target)
    {
        int rc = pthread_cond_timedwait(cond, lock, &ts);
        if (rc == ETIMEDOUT)
            break;
    }
    return *value >= target;
}

static void task_inc(void *arg)
{
    CountSync *sync = (CountSync *)arg;
    pthread_mutex_lock(&sync->lock);
    sync->completed++;
    pthread_cond_broadcast(&sync->cond);
    pthread_mutex_unlock(&sync->lock);
}

static void task_block(void *arg)
{
    BlockSync *sync = (BlockSync *)arg;
    pthread_mutex_lock(&sync->lock);
    sync->started++;
    pthread_cond_broadcast(&sync->cond);
    while (!sync->release)
        pthread_cond_wait(&sync->cond, &sync->lock);
    sync->completed++;
    pthread_cond_broadcast(&sync->cond);
    pthread_mutex_unlock(&sync->lock);
}

Test(thread_pool, executes_tasks)
{
    CountSync sync = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .completed = 0,
        .target = 5,
    };

    ThreadPool *pool = thread_pool_create(1, 2);
    cr_assert_not_null(pool, "pool create");

    for (int i = 0; i < sync.target; i++)
        cr_assert(thread_pool_add_task(pool, task_inc, &sync));

    pthread_mutex_lock(&sync.lock);
    int ok = wait_for_value(&sync.cond, &sync.lock, &sync.completed, sync.target, 1000);
    pthread_mutex_unlock(&sync.lock);
    cr_assert(ok, "tasks did not complete");

    thread_pool_destroy(pool);
}

Test(thread_pool, grows_under_load)
{
    BlockSync sync = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .started = 0,
        .completed = 0,
        .release = 0,
        .total = 4,
    };

    ThreadPool *pool = thread_pool_create(1, 2);
    cr_assert_not_null(pool, "pool create");

    for (int i = 0; i < sync.total; i++)
        cr_assert(thread_pool_add_task(pool, task_block, &sync));

    pthread_mutex_lock(&sync.lock);
    int ok = wait_for_value(&sync.cond, &sync.lock, &sync.started, 2, 1000);
    if (!ok) {
        sync.release = 1;
        pthread_cond_broadcast(&sync.cond);
    }
    pthread_mutex_unlock(&sync.lock);
    cr_assert(ok, "expected pool to start 2 tasks under load");

    pthread_mutex_lock(&sync.lock);
    sync.release = 1;
    pthread_cond_broadcast(&sync.cond);
    ok = wait_for_value(&sync.cond, &sync.lock, &sync.completed, sync.total, 2000);
    pthread_mutex_unlock(&sync.lock);
    cr_assert(ok, "expected all tasks to complete");

    thread_pool_destroy(pool);
}

Test(thread_pool, clamps_min_threads_to_max_threads)
{
    CountSync sync = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .completed = 0,
        .target = 3,
    };

    ThreadPool *pool = thread_pool_create(4, 2);
    cr_assert_not_null(pool, "pool create should normalize min/max thread counts");

    for (int i = 0; i < sync.target; i++)
        cr_assert(thread_pool_add_task(pool, task_inc, &sync));

    pthread_mutex_lock(&sync.lock);
    int ok = wait_for_value(&sync.cond, &sync.lock, &sync.completed, sync.target, 1000);
    pthread_mutex_unlock(&sync.lock);
    cr_assert(ok, "tasks did not complete");

    thread_pool_destroy(pool);
}
