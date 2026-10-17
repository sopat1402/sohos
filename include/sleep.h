#ifndef SLEEP
#define SLEEP

#include <stdint.h>
#include "wait_queue.h"

struct sleep_entry {
    struct sleep_entry *next;
    uint64_t deadline;
    struct wait_queue waitq;
    struct wait_queue_entry waiter;
};

void sleep_ticks_tick(void);
void sleep_ticks(uint64_t ticks);

#endif
