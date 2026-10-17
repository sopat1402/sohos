#ifndef WAITQ
#define WAITQ

#include <stdint.h>
#include "spinlock.h"

struct wait_queue_entry {
    struct thread *thread;
    struct wait_queue_entry *next;
};

struct wait_queue {
    struct wait_queue_entry *head;
    struct wait_queue_entry *tail;
    spinlock_t lock;
};
void wait_queue_init(struct wait_queue *q);
void wait_queue_add(struct wait_queue *q, struct wait_queue_entry *entry, struct thread *thread);
int wait_queue_remove(struct wait_queue *q, struct wait_queue_entry *entry);
struct thread *wait_queue_take_one(struct wait_queue *q);
int wait_queue_prepare_to_wait(struct wait_queue *q, struct wait_queue_entry *entry, uint64_t *saved_flags);
int wait_queue_wake_one(struct wait_queue *q);

#endif
