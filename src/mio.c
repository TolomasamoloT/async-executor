#include "mio.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "debug.h"
#include "executor.h"
#include "waker.h"

// i think this should have been provided by default if we re expected to use it
#include "err.h"

// Maximum number of events to handle per epoll_wait call.
#define MAX_EVENTS 64

struct Mio {
    int epoll_fd;
    Executor *executor;
    int waiting;
};

Mio* mio_create(Executor* executor) {
    Mio* mio = malloc(sizeof(Mio));
    if (!mio) {
        return NULL;
    }

    mio->executor = executor;
    mio->waiting = 0;
    mio->epoll_fd = epoll_create1(0);

	if (mio->epoll_fd == -1) {
		free(mio);
        return NULL;
	}

    return mio;
}

void mio_destroy(Mio* mio) {
    if (!mio) fatal("trying to destroy a non existant MIO instance");

    if (close(mio->epoll_fd)) {
        free(mio);
        fatal("Failed to close epoll file descriptor");
	}

    free(mio);
}

int mio_register(Mio* mio, int fd, uint32_t events, Waker waker)
{
    debug("Registering (in Mio = %p) fd = %d with\n", mio, fd);

	struct epoll_event event;
    event.events = events;
    event.data.ptr = waker.future;

    if(epoll_ctl(mio->epoll_fd, EPOLL_CTL_ADD, fd, &event)) {
        // modifying event waiting on fd
        // we were already watchin that fd, so we just update the ptr to the ccurent one
        if (epoll_ctl(mio->epoll_fd, EPOLL_CTL_MOD, fd, &event)) {
            return -1;  // we failed to modify
        }
        // we modified correctly so the number of waiting won't change
        // so we decrase it now and inc it later
        mio->waiting--;
	}

    mio->waiting++;
    return 0;
}

int mio_unregister(Mio* mio, int fd)
{
    debug("Unregistering (from Mio = %p) fd = %d\n", mio, fd);

    if(epoll_ctl(mio->epoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
		return -1;
	}

    mio->waiting--;
    return 0;
}

void mio_poll(Mio* mio)
{
    debug("Mio (%p) polling\n", mio);

    if (mio->waiting == 0) return;

	struct epoll_event events[MAX_EVENTS];

    int event_count = epoll_wait(mio->epoll_fd, events, MAX_EVENTS, -1);

    for (int i = 0; i < event_count; i++) {
        Future *fut = events[i].data.ptr;
        Waker waker = {.executor = mio->executor, .future = fut};

        waker_wake(&waker);
    }
}