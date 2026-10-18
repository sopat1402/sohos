#include <stdint.h>
#include <stddef.h>
#include "../include/thread.h"
#include "../include/wait_queue.h"
#include "../include/spinlock.h"
#include "../include/sched.h"
#include "../include/mutex.h"

void mutex_lock(mutex_t *mutex) {
    struct thread *thrd = thread_current();
    for (;;) {
        uint64_t flags = spin_lock_irqsave(&mutex->guard);
        if (mutex->owner == thrd) {
            mutex->recursion_depth++;
            spin_unlock_irqrestore(&mutex->guard, flags);
            return;
        }
        if (mutex->owner == NULL) {
            mutex->owner = thrd;
            mutex->recursion_depth=1;
            spin_unlock_irqrestore(&mutex->guard, flags);
            return;
        }
        spin_unlock_irqrestore(&mutex->guard, flags);
        struct wait_queue_entry entry = {0};
        uint64_t wait_flags;
        if (!wait_queue_prepare_to_wait(&mutex->waiters, &entry, &wait_flags))
            continue;
        flags = spin_lock_irqsave(&mutex->guard);
        if (mutex->owner == NULL) {
            mutex->owner = thrd;
            mutex->recursion_depth=1;
            spin_unlock_irqrestore(&mutex->guard, flags);
            sched_cancel_block_current();
            wait_queue_remove(&mutex->waiters, &entry);
            irq_restore(wait_flags);
            return;
        }
        spin_unlock_irqrestore(&mutex->guard, flags);
        schedule();
        wait_queue_remove(&mutex->waiters, &entry);
        irq_restore(wait_flags);
    }
}

void mutex_unlock(mutex_t *mutex){
    struct thread *thrd=thread_current();
    uint64_t flags=spin_lock_irqsave(&mutex->guard);
    if (mutex->owner!=thrd){
        spin_unlock_irqrestore(&mutex->guard,flags);
        return;
    }
    mutex->recursion_depth-=1;
    int was_zero=0;
    if (mutex->recursion_depth ==0) {
        mutex->owner=NULL;
        was_zero=1;
    }
    spin_unlock_irqrestore(&mutex->guard,flags);
    if (was_zero) wait_queue_wake_one(&mutex->waiters);
}

void mutex_init(mutex_t *mutex) {
    mutex->owner = NULL;
    mutex->guard = (spinlock_t)SPINLOCK_INIT;
    mutex->recursion_depth=0;
    wait_queue_init(&mutex->waiters);
}
