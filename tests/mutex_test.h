#ifndef SOHOS_MUTEX_RACE_TEST_H
#define SOHOS_MUTEX_RACE_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"
#include "../include/percpu.h"
#include "../include/thread.h"
#include "../include/sched.h"
#include "../include/mutex.h"

#define MT_THREADS 4u
#define MT_ITERS 2000u
#define MT_TIMEOUT_TICKS 3000u
#define MT_MAX_REPORTS 10u

#define MT_CHECK(cond, name) do { mt_checks++; if (!(cond)) { mt_fails++; if (mt_fails <= MT_MAX_REPORTS) { print("[mutex] FAIL "); print(name); print("\n"); } } } while (0)

struct mt_arg {
    uint32_t id;
    uint32_t cpu;
    uint32_t progress;
    uint32_t stage;
};

static uint64_t mt_checks;
static uint64_t mt_fails;
static mutex_t mt_mutex;
static uint64_t mt_counter;
static uint32_t mt_done;
static uint32_t mt_inside;
static uint32_t mt_bad;
static struct mt_arg mt_args[MT_THREADS];

static uint32_t mt_load(uint32_t *v) {
    return __atomic_load_n(v, __ATOMIC_ACQUIRE);
}

static void mt_store(uint32_t *v, uint32_t x) {
    __atomic_store_n(v, x, __ATOMIC_RELEASE);
}

static uint64_t mt_now(void) {
    return percpu_self()->ticks;
}

static int mt_wait_ge(uint32_t *v, uint32_t target, uint32_t timeout) {
    uint64_t start = mt_now();
    while (mt_load(v) < target) {
        if (mt_now() - start > timeout)
            return 0;
        sched_yield();
    }
    return 1;
}

static const char *mt_stage_name(uint32_t stage) {
    switch (stage) {
        case 1: return "acquiring mutex";
        case 2: return "recursive acquisition";
        case 3: return "inside critical section";
        case 4: return "first recursive unlock";
        case 5: return "yield after first unlock";
        case 6: return "final unlock";
        default: return "idle";
    }
}

static void mt_report(const char *name, uint64_t before) {
    print("[test] ");
    print(name);
    print(mt_fails == before ? " ok\n" : " FAILED\n");
}

static void mt_worker(void *arg) {
    struct mt_arg *a = (struct mt_arg *)arg;

    for (uint32_t i = 0; i < MT_ITERS; i++) {
        mt_store(&a->stage, 1);
        mutex_lock(&mt_mutex);
        mt_store(&a->stage, 2);
        mutex_lock(&mt_mutex);
        mt_store(&a->stage, 3);

        if (__atomic_exchange_n(&mt_inside, 1, __ATOMIC_ACQ_REL) != 0)
            __atomic_store_n(&mt_bad, 1, __ATOMIC_RELEASE);

        __atomic_fetch_add(&mt_counter, 1, __ATOMIC_RELAXED);
        if ((i & 31u) == 0)
            sched_yield();

        mt_store(&a->stage, 4);
        mutex_unlock(&mt_mutex);
        mt_store(&a->stage, 5);
        sched_yield();
        mt_store(&mt_inside, 0);

        mt_store(&a->stage, 6);
        mutex_unlock(&mt_mutex);
        mt_store(&a->progress, i + 1);
        mt_store(&a->stage, 0);
    }

    __atomic_fetch_add(&mt_done, 1, __ATOMIC_RELEASE);
}

static void mt_test_recursion(void) {
    mutex_init(&mt_mutex);
    struct thread *self = thread_current();

    mutex_lock(&mt_mutex);
    mutex_lock(&mt_mutex);
    MT_CHECK(mt_mutex.owner == self, "recursive acquisition retains owner");
    MT_CHECK(mt_mutex.recursion_depth == 2, "recursive depth reaches two");

    mutex_unlock(&mt_mutex);
    MT_CHECK(mt_mutex.owner == self && mt_mutex.recursion_depth == 1, "first unlock retains ownership");

    mutex_unlock(&mt_mutex);
    MT_CHECK(mt_mutex.owner == NULL && mt_mutex.recursion_depth == 0, "final unlock releases ownership");
}

static void mt_test_contention(void) {
    uint32_t cpus = percpu_count();
    MT_CHECK(cpus > 0, "CPU count nonzero");
    if (cpus == 0)
        return;

    mutex_init(&mt_mutex);
    __atomic_store_n(&mt_counter, 0, __ATOMIC_RELAXED);
    mt_store(&mt_done, 0);
    mt_store(&mt_inside, 0);
    mt_store(&mt_bad, 0);

    uint32_t started = 0;
    print("[mutex] creating contention workers\n");

    for (uint32_t i = 0; i < MT_THREADS; i++) {
        mt_args[i].id = i;
        mt_args[i].cpu = i % cpus;
        mt_store(&mt_args[i].progress, 0);
        mt_store(&mt_args[i].stage, 0);

        if (thread_create_on("mutexrace", mt_worker, &mt_args[i], mt_args[i].cpu) == NULL) {
            print("[mutex] failed to create worker ");
            print_uint(i);
            print("\n");
            break;
        }

        started++;
        print("[mutex] worker ");
        print_uint(i);
        print(" assigned to CPU ");
        print_uint(mt_args[i].cpu);
        print("\n");
    }

    MT_CHECK(started == MT_THREADS, "all contention workers created");
    if (started == 0)
        return;

    print("[mutex] waiting for ");
    print_uint(started);
    print(" workers\n");

    int finished = mt_wait_ge(&mt_done, started, MT_TIMEOUT_TICKS);
    if (!finished) {
        print("[mutex] TIMEOUT: completed ");
        print_uint(mt_load(&mt_done));
        print(" / ");
        print_uint(started);
        print(" workers\n");

        for (uint32_t i = 0; i < started; i++) {
            print("[mutex] worker ");
            print_uint(i);
            print(" CPU=");
            print_uint(mt_args[i].cpu);
            print(" progress=");
            print_uint(mt_load(&mt_args[i].progress));
            print("/");
            print_uint(MT_ITERS);
            print(" stage=");
            print(mt_stage_name(mt_load(&mt_args[i].stage)));
            print("\n");
        }

        MT_CHECK(finished, "workers completed before timeout; timeout may indicate deadlock or lost wakeup");
        return;
    }

    uint64_t expected = (uint64_t)started * MT_ITERS;
    uint64_t actual = __atomic_load_n(&mt_counter, __ATOMIC_ACQUIRE);

    print("[mutex] completed iterations: ");
    print_uint(actual);
    print(" / ");
    print_uint(expected);
    print("\n");

    MT_CHECK(actual == expected, "all critical-section increments completed");
    MT_CHECK(mt_load(&mt_bad) == 0, "critical sections never overlapped");
    MT_CHECK(mt_load(&mt_inside) == 0, "critical section flag cleared");
}

static inline int mutex_race_test_run(void) {
    mt_checks = 0;
    mt_fails = 0;
    print("Running mutex tests...\n");

    uint64_t mark = mt_fails;
    mt_test_recursion();
    mt_report("recursive locking", mark);

    mark = mt_fails;
    mt_test_contention();
    mt_report("concurrent contention and wakeups", mark);

    print("Mutex tests: ");
    print_uint(mt_checks);
    print(" checks, ");
    print_uint(mt_fails);
    print(" failed\n");
    return mt_fails == 0;
}

#endif
