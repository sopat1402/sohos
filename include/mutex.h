#ifndef MUTEX
#define MUTEX

#include <stdint.h>
#include "thread.h"
#include "wait_queue.h"

typedef struct mutex {
    struct thread *owner;
    spinlock_t guard;
    struct wait_queue waiters;
    uint64_t recursion_depth;
} mutex_t;

void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
void mutex_init(mutex_t *mutex);

#endif
