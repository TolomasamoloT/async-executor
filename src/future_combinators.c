#include "future_combinators.h"
#include <stdlib.h>

#include "future.h"
#include "waker.h"

static FutureState then_future_progress(Future* fut, Mio* mio, Waker waker) {
    ThenFuture* self = (ThenFuture*)fut;
    
    // First progress fut1 if not already completed
    if (!self->fut1_completed) {
        // we pass our waker so when fd becomes avalible it wakes us up and we progress fut1 further
        FutureState fut1_state = self->fut1->progress(self->fut1, mio, waker);
        if (fut1_state == FUTURE_PENDING) {
            return FUTURE_PENDING;
        } else if (fut1_state == FUTURE_FAILURE) {
            self->base.errcode = THEN_FUTURE_ERR_FUT1_FAILED;
            return FUTURE_FAILURE;
        }
        // fut1 completed successfully
        self->fut1_completed = true;
        self->fut2->arg = self->fut1->ok;
    }
    
    // Now progress fut2 using the same trick
    FutureState fut2_state = self->fut2->progress(self->fut2, mio, waker);
    if (fut2_state == FUTURE_PENDING) {
        return FUTURE_PENDING;
    } else if (fut2_state == FUTURE_FAILURE) {
        self->base.errcode = THEN_FUTURE_ERR_FUT2_FAILED;
        return FUTURE_FAILURE;
    }
    
    // fut2 completed successfully
    self->base.ok = self->fut2->ok;
    
    return FUTURE_COMPLETED;
}

ThenFuture future_then(Future* fut1, Future* fut2) {
    return (ThenFuture) {
        .base = future_create(then_future_progress),
        .fut1 = fut1,
        .fut2 = fut2,
        .fut1_completed = false
    };
}

/**
 * A wrapper that executes its future
 *
 * yap yap yap
 */
typedef struct JoinWrapperFuture {
    Future base; // Base future structure
    Future* child; // Future we're wrapping around
    JoinFuture* parent; // we change stuff directly inside the parent, and wake him up if necesary
    int id;
} JoinWrapperFuture;

static FutureState join_wrapper_future_progress(Future* fut, Mio* mio, Waker waker) {
    JoinWrapperFuture* self = (JoinWrapperFuture*)fut;

    // time for a good old magic trick:
    // we pass our waker so when fd becomes avalible it wakes us up and we progress fut further
    FutureState state = self->child->progress(self->child, mio, waker);

    if (state == FUTURE_PENDING) {
        return FUTURE_PENDING;
    }

    // we finished in some way
    if (self->id == 1) {    // we're executing fut1
        // set our data
        self->parent->fut1_completed = state;
        self->parent->result.fut1.errcode = self->child->errcode;
        self->parent->result.fut1.ok = self->child->ok;

        // check if there is already data from fut2 => we need to wake up parent
        if (self->parent->fut2_completed != FUTURE_PENDING) {
            Waker w = {.executor = waker.executor, .future = (Future*)(self->parent)};
            waker_wake(&w);
        }
    } else {                // we're executing fut2
        // set our data
        self->parent->fut2_completed = state;
        self->parent->result.fut2.errcode = self->child->errcode;
        self->parent->result.fut2.ok = self->child->ok;
        
        // check if there is already data from fut1 => we need to wake up parent
        if (self->parent->fut1_completed != FUTURE_PENDING) {
            Waker w = {.executor = waker.executor, .future = (Future*)(self->parent)};
            waker_wake(&w);
        }
    }


    // time for another dirty trick
    // we need to free our memory, since i don't want to hold it in the JoinFuture
    free(self);
    // however this would cause problems in executor_run
    // since the executor would try to set is_active to false
    // so instead of: return state;
    // we return 
    return FUTURE_PENDING;
}

// we need to malloc :c

// JoinWrapperFuture wraper_future_create(Future* child, JoinFuture* parent, int id) {
//     return (JoinWrapperFuture) {
//         .base = future_create(wrapper_future_progress),
//         .child = child,
//         .parent = parent,
//         .id = id
//     };
// }

static FutureState join_future_progress(Future* fut, Mio* mio, Waker waker) {
    JoinFuture* self = (JoinFuture*)fut;

    // if it's the 1st time its called we create children and send them off to execution D:
    if (self->fut1_completed == FUTURE_PENDING && self->fut2_completed == FUTURE_PENDING) {
        JoinWrapperFuture* child1 = malloc(sizeof(JoinWrapperFuture));
        // if (!child1) fatal(" once again no err.h so im not doing malloc fail logic :P ");

        child1->base = future_create(join_wrapper_future_progress);
        child1->child = self->fut1;
        child1->parent = self;
        child1->id = 1;

        Waker w1 = {.executor = waker.executor, .future = (Future*)child1};
        waker_wake(&w1);

        JoinWrapperFuture* child2 = malloc(sizeof(JoinWrapperFuture));
        // if (!child2) fatal(" once again no err.h so im not doing malloc fail logic :P ");

        child2->base = future_create(join_wrapper_future_progress);
        child2->child = self->fut2;
        child2->parent = self;
        child2->id = 2;

        Waker w2 = {.executor = waker.executor, .future = (Future*)child2};
        waker_wake(&w2);

        // go to sleep, waiting for children
        return FUTURE_PENDING;
    }

    // since we re here we have been awoken by our children
    
    // Both futures have completed or failed
    if (self->fut1_completed == FUTURE_FAILURE && self->fut2_completed == FUTURE_FAILURE) {
        self->base.errcode = JOIN_FUTURE_ERR_BOTH_FUTS_FAILED;
        return FUTURE_FAILURE;
    } else if (self->fut1_completed == FUTURE_FAILURE) {
        self->base.errcode = JOIN_FUTURE_ERR_FUT1_FAILED;
        return FUTURE_FAILURE;
    } else if (self->fut2_completed == FUTURE_FAILURE) {
        self->base.errcode = JOIN_FUTURE_ERR_FUT2_FAILED;
        return FUTURE_FAILURE;
    }
    
    return FUTURE_COMPLETED;
}

JoinFuture future_join(Future* fut1, Future* fut2) {
    return (JoinFuture) {
        .base = future_create(join_future_progress),
        .fut1 = fut1,
        .fut2 = fut2,
        .fut1_completed = FUTURE_PENDING,
        .fut2_completed = FUTURE_PENDING,
        .result = { { 0, NULL }, { 0, NULL } }
    };
}


/**
 * A wrapper that executes its future
 *
 * yap yap yap
 */
typedef struct SelectWrapperFuture {
    Future base; // Base future structure
    Future* child; // Future we're wrapping around
    SelectFuture* parent; // we change stuff directly inside the parent, and wake him up if necesary
    int id;
} SelectWrapperFuture;

static FutureState select_wrapper_future_progress(Future* fut, Mio* mio, Waker waker) {
    SelectWrapperFuture* self = (SelectWrapperFuture*)fut;

    // time for a good old magic trick:
    // we pass our waker so when fd becomes avalible it wakes us up and we progress fut further
    FutureState state = self->child->progress(self->child, mio, waker);

    if (state == FUTURE_PENDING) {
        return FUTURE_PENDING;
    }

    // we finished in some way

    // theese checks need to be here and not earlier so that our child correctly
    // unsubscribes from mio

    // if parent no longer exists we kill ourself
    if (!self->parent) {
        free(self);
        return FUTURE_PENDING;
    }

    // if the other future completed b4 us we kill ourself
    if (self->id == 1) {
        if (self->parent->which_completed == SELECT_COMPLETED_FUT2) {
            free(self);
            return FUTURE_PENDING;
        }
    } else {
        if (self->parent->which_completed == SELECT_COMPLETED_FUT1) {
            free(self);
            return FUTURE_PENDING;
        }
    }

    if (self->id == 1) {    // we're executing fut1
        if (state == FUTURE_COMPLETED) {
            self->parent->which_completed = SELECT_COMPLETED_FUT1;
            Waker w = {.executor = waker.executor, .future = (Future*)(self->parent)};
            waker_wake(&w);
        } else {
            if (self->parent->which_completed == SELECT_COMPLETED_NONE) {
                self->parent->which_completed = SELECT_FAILED_FUT1;
            } else if (self->parent->which_completed == SELECT_FAILED_FUT2) {
                self->parent->which_completed = SELECT_FAILED_BOTH;
                self->parent->base.errcode = self->child->errcode;
                Waker w = {.executor = waker.executor, .future = (Future*)(self->parent)};
                waker_wake(&w);
            }
        }
    } else {                // we're executing fut2
        if (state == FUTURE_COMPLETED) {
            self->parent->which_completed = SELECT_COMPLETED_FUT2;
            Waker w = {.executor = waker.executor, .future = (Future*)(self->parent)};
            waker_wake(&w);
        } else {
            if (self->parent->which_completed == SELECT_COMPLETED_NONE) {
                self->parent->which_completed = SELECT_FAILED_FUT2;
            } else if (self->parent->which_completed == SELECT_FAILED_FUT1) {
                self->parent->which_completed = SELECT_FAILED_BOTH;
                self->parent->base.errcode = self->child->errcode;
                Waker w = {.executor = waker.executor, .future = (Future*)(self->parent)};
                waker_wake(&w);
            }
        }
    }

    // time for another dirty trick
    // we need to free our memory, since i don't want to hold it in the JoinFuture
    free(self);
    // however this would cause problems in executor_run
    // since the executor would try to set is_active to false
    // so instead of: return state;
    // we return 
    return FUTURE_PENDING;
}

static FutureState select_future_progress(Future* fut, Mio* mio, Waker waker) {
    SelectFuture* self = (SelectFuture*)fut;

    // if it's the 1st time its called we create children and send them off to execution D:
    if (self->which_completed == SELECT_COMPLETED_NONE) {
        SelectWrapperFuture* child1 = malloc(sizeof(SelectWrapperFuture));
        // if (!child1) fatal(" once again no err.h so im not doing malloc fail logic :P ");

        child1->base = future_create(select_wrapper_future_progress);
        child1->child = self->fut1;
        child1->parent = self;
        child1->id = 1;

        Waker w1 = {.executor = waker.executor, .future = (Future*)child1};
        waker_wake(&w1);

        SelectWrapperFuture* child2 = malloc(sizeof(SelectWrapperFuture));
        // if (!child2) fatal(" once again no err.h so im not doing malloc fail logic :P ");

        child2->base = future_create(select_wrapper_future_progress);
        child2->child = self->fut2;
        child2->parent = self;
        child2->id = 2;

        Waker w2 = {.executor = waker.executor, .future = (Future*)child2};
        waker_wake(&w2);

        // go to sleep, waiting for children
        return FUTURE_PENDING;
    }

    // since we re here we have been awoken by our children
    
    // Both futures have completed or failed
    if (self->which_completed == SELECT_FAILED_BOTH) {
        return FUTURE_FAILURE;
    }
    
    return FUTURE_COMPLETED;
}

SelectFuture future_select(Future* fut1, Future* fut2) {
    return (SelectFuture) {
        .base = future_create(select_future_progress),
        .fut1 = fut1,
        .fut2 = fut2,
        .which_completed = SELECT_COMPLETED_NONE
    };
}