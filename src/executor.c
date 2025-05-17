#include "executor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "future.h"
#include "mio.h"
#include "waker.h"

// i think this should have been provided by default if we re expected to use it
#include "err.h"

/**
 * @brief Structure to represent the current-thread executor.
 */
struct Executor {
    Future **queue;
    size_t queue_size;
    size_t max_queue_size;
    size_t head;
    size_t tail;
    Mio *mio;
};

Executor* executor_create(size_t max_queue_size) {
    Executor* exe = malloc(sizeof(Executor));
    if (!exe) fatal("malloc failure while trying to create executor");

    exe->mio = mio_create(exe);
    if (!exe->mio) {
        free(exe);
        fatal("failed to create MIO");
    }

    exe->queue = malloc(max_queue_size * sizeof(Future *));
    if (!exe->queue) {
        mio_destroy(exe->mio);
        free(exe);
        fatal("malloc failure while trying to create executor queue");
    }

    exe->max_queue_size = max_queue_size;
    exe->queue_size = 0;
    exe->head = 0;
    exe->tail = 0;

    return exe;
}

void waker_wake(Waker* waker) {
    executor_spawn(waker->executor, (Future*)waker->future);
}

void executor_spawn(Executor* executor, Future* fut) {
    if (executor->queue_size >= executor->max_queue_size) {
        executor_destroy(executor);
        fatal("trying to add more futures than is allowed by the executor max_queue_size");
    }

    fut->is_active = true;
    executor->queue[executor->tail]  = fut;
    executor->tail = (executor->tail + 1) % executor->max_queue_size;
    executor->queue_size++;
}

void executor_run(Executor* executor) {
    while (executor->queue_size > 0) {
        while (executor->queue_size > 0) {
            Future *fut = executor->queue[executor->head];
            executor->head = (executor->head + 1) % executor->max_queue_size;
            executor->queue_size--;

            Waker waker = {.executor = executor, .future = fut};
            FutureState state = fut->progress(fut, executor->mio, waker);

            if (state == FUTURE_COMPLETED || state == FUTURE_FAILURE) {
                fut->is_active = false;
            }
        }

        mio_poll(executor->mio);
    }
}

void executor_destroy(Executor* executor) {
    if (!executor) fatal("trying to destroy a non existant executor instance");
    free(executor->queue);
    mio_destroy(executor->mio);
    free(executor);
}