#include "../include/spinlock.h"
#include "../include/thread.h"
#include "../include/percpu.h"
#include "../include/sched.h"
#include "../include/wait_queue.h"
#include <stdint.h>
#include <stddef.h>

void wait_queue_init(struct wait_queue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->lock = (spinlock_t)SPINLOCK_INIT;
}

void wait_queue_add(struct wait_queue *q, struct wait_queue_entry *entry, struct thread *thread) {
    entry->thread = thread;
    entry->next = NULL;

    uint64_t flags = spin_lock_irqsave(&q->lock);

    if (q->tail != NULL)
        q->tail->next = entry;
    else
        q->head = entry;

    q->tail = entry;

    spin_unlock_irqrestore(&q->lock, flags);
}

int wait_queue_remove(struct wait_queue *q, struct wait_queue_entry *entry) {
    uint64_t flags = spin_lock_irqsave(&q->lock);
    struct wait_queue_entry *prev = NULL;
    struct wait_queue_entry *curr = q->head;

    while (curr != NULL && curr != entry) {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL) {
        spin_unlock_irqrestore(&q->lock, flags);
        return 0;
    }

    if (prev != NULL)
        prev->next = curr->next;
    else
        q->head = curr->next;

    if (q->tail == curr)
        q->tail = prev;

    curr->next = NULL;

    spin_unlock_irqrestore(&q->lock, flags);
    return 1;
}

struct thread *wait_queue_take_one(struct wait_queue *q) {
    uint64_t flags = spin_lock_irqsave(&q->lock);
    struct wait_queue_entry *entry = q->head;

    if (entry == NULL) {
        spin_unlock_irqrestore(&q->lock, flags);
        return NULL;
    }

    q->head = entry->next;

    if (q->head == NULL)
        q->tail = NULL;

    entry->next = NULL;
    struct thread *thread = entry->thread;

    spin_unlock_irqrestore(&q->lock, flags);
    return thread;
}

int wait_queue_prepare_to_wait(struct wait_queue *q, struct wait_queue_entry *entry, uint64_t *saved_flags) {
    if (q == NULL || entry == NULL || saved_flags == NULL)
        return 0;

    *saved_flags = irq_save();

    struct percpu *p = percpu_self();
    struct thread *t = p != NULL ? (struct thread *)p->current_thread : NULL;

    if (t == NULL) {
        irq_restore(*saved_flags);
        return 0;
    }

    uint64_t q_flags = spin_lock_irqsave(&q->lock);

    if (!sched_prepare_block_current()) {
        spin_unlock_irqrestore(&q->lock, q_flags);
        irq_restore(*saved_flags);
        return 0;
    }

    entry->thread = t;
    entry->next = NULL;

    if (q->tail != NULL)
        q->tail->next = entry;
    else
        q->head = entry;

    q->tail = entry;

    spin_unlock_irqrestore(&q->lock, q_flags);
    return 1;
}

int wait_queue_wake_one(struct wait_queue *q) {
    if (q == NULL)
        return 0;
    uint64_t flags = spin_lock_irqsave(&q->lock);
    int woke = 0;
    while (q->head != NULL) {
        struct wait_queue_entry *entry = q->head;
        q->head = entry->next;
        if (q->head == NULL)
            q->tail = NULL;
        entry->next = NULL;
        if (sched_wake_thread(entry->thread)) {
            woke = 1;
            break;
        }
    }
    spin_unlock_irqrestore(&q->lock, flags);
    return woke;
}
