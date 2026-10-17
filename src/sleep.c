#include <stdint.h>
#include <stddef.h>
#include "../include/percpu.h"
#include "../include/sched.h"
#include "../include/wait_queue.h"
#include "../include/spinlock.h"
#include "../include/sleep.h"

#define SLEEP_MAX_CPUS 256u
#define SLEEP_HALF_RANGE (1ULL << 63)

static struct sleep_entry *sleep_heads[SLEEP_MAX_CPUS];

static int sleep_tick_before(uint64_t a, uint64_t b) {
    return a != b && (b - a) < SLEEP_HALF_RANGE;
}

static int sleep_tick_expired(uint64_t now, uint64_t deadline) {
    return (now - deadline) < SLEEP_HALF_RANGE;
}

static void sleep_insert(uint32_t cpu, struct sleep_entry *entry) {
    struct sleep_entry **link = &sleep_heads[cpu];

    while (*link && !sleep_tick_before(entry->deadline, (*link)->deadline))
        link = &(*link)->next;

    entry->next = *link;
    *link = entry;
}

static struct sleep_entry *sleep_pop_expired(uint32_t cpu, uint64_t now) {
    struct sleep_entry *entry = sleep_heads[cpu];

    if (!entry || !sleep_tick_expired(now, entry->deadline))
        return NULL;

    sleep_heads[cpu] = entry->next;
    entry->next = NULL;
    return entry;
}

void sleep_ticks(uint64_t ticks) {
    if (ticks == 0)
        return;

    struct percpu *cpu = percpu_self();
    uint32_t id = cpu->cpu_id;

    if (id >= SLEEP_MAX_CPUS)
        return;

    if (ticks >= SLEEP_HALF_RANGE)
        ticks = SLEEP_HALF_RANGE - 1;

    struct sleep_entry entry = {0};
    entry.deadline = __atomic_load_n(&cpu->ticks, __ATOMIC_RELAXED) + ticks;
    wait_queue_init(&entry.waitq);

    uint64_t flags;
    if (!wait_queue_prepare_to_wait(&entry.waitq, &entry.waiter, &flags))
        return;

    sleep_insert(id, &entry);

    schedule();

    wait_queue_remove(&entry.waitq, &entry.waiter);
    irq_restore(flags);
}

void sleep_ticks_tick(void) {
    struct percpu *cpu = percpu_self();
    uint32_t id = cpu->cpu_id;

    if (id >= SLEEP_MAX_CPUS)
        return;

    uint64_t now = __atomic_load_n(&cpu->ticks, __ATOMIC_RELAXED);

    for (;;) {
        struct sleep_entry *entry = sleep_pop_expired(id, now);

        if (!entry)
            break;

        wait_queue_wake_one(&entry->waitq);
    }
}
