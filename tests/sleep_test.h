#ifndef SLEEP_TEST_H
#define SLEEP_TEST_H

#include <stdint.h>
#include "../include/sleep.h"
#include "../include/thread.h"
#include "../include/sched.h"
#include "../include/percpu.h"
#include "../include/display.h"

#define SLEEP_TEST_CASES 3u
#define SLEEP_TEST_TIMEOUT 1500u

struct sleep_test_case {
    uint32_t requested;
    uint64_t elapsed;
    uint32_t cpu_before;
    uint32_t cpu_after;
};

static struct sleep_test_case sleep_test_cases[SLEEP_TEST_CASES];
static uint32_t sleep_test_done;
static uint32_t sleep_test_checks;
static uint32_t sleep_test_fails;

static const uint32_t sleep_test_delays[SLEEP_TEST_CASES] = {
    20, 50, 100
};

static void sleep_test_check(int condition, const char *name) {
    sleep_test_checks++;

    if (condition) {
        print("[sleep] PASS ");
    } else {
        sleep_test_fails++;
        print("[sleep] FAIL ");
    }

    print(name);
    print("\n");
}

static void sleep_test_worker(void *arg) {
    struct sleep_test_case *c = (struct sleep_test_case *)arg;
    struct percpu *p = percpu_self();

    c->cpu_before = p->cpu_id;
    uint64_t start = __atomic_load_n(&p->ticks, __ATOMIC_RELAXED);

    sleep_ticks(c->requested);

    p = percpu_self();
    uint64_t end = __atomic_load_n(&p->ticks, __ATOMIC_RELAXED);

    c->cpu_after = p->cpu_id;
    c->elapsed = end - start;

    __atomic_fetch_add(&sleep_test_done, 1, __ATOMIC_RELEASE);
}

static int sleep_test_run(void) {
    sleep_test_checks = 0;
    sleep_test_fails = 0;
    __atomic_store_n(&sleep_test_done, 0, __ATOMIC_RELEASE);

    print("Running sleep tests...\n");

    uint32_t cpu = percpu_self()->cpu_id;
    struct percpu *target = percpu_by_cpu_id(cpu);

    if (target == 0) {
        print("[sleep] Cannot find current CPU\n");
        return 0;
    }

    uint32_t created = 0;
    uint32_t launched[SLEEP_TEST_CASES] = {0};

    for (uint32_t i = 0; i < SLEEP_TEST_CASES; i++) {
        sleep_test_cases[i].requested = sleep_test_delays[i];
        sleep_test_cases[i].elapsed = 0;
        sleep_test_cases[i].cpu_before = UINT32_MAX;
        sleep_test_cases[i].cpu_after = UINT32_MAX;

        if (thread_create_on("sleeper", sleep_test_worker,
                             &sleep_test_cases[i], cpu) != NULL) {
            launched[i] = 1;
            created++;
        }
    }

    sleep_test_check(created == SLEEP_TEST_CASES,
                     "all sleeper threads created");

    if (created == 0)
        goto done;

    {
        uint64_t start = __atomic_load_n(&target->ticks, __ATOMIC_RELAXED);

        while (__atomic_load_n(&sleep_test_done, __ATOMIC_ACQUIRE) < created) {
            uint64_t now = __atomic_load_n(&target->ticks, __ATOMIC_RELAXED);

            if (now - start >= SLEEP_TEST_TIMEOUT)
                break;

            sched_yield();
        }
    }

    sleep_test_check(
        __atomic_load_n(&sleep_test_done, __ATOMIC_ACQUIRE) == created,
        "sleepers completed before timeout");

    if (__atomic_load_n(&sleep_test_done, __ATOMIC_ACQUIRE) != created)
        goto done;

    for (uint32_t i = 0; i < SLEEP_TEST_CASES; i++) {
        if (!launched[i])
            continue;

        sleep_test_check(
            sleep_test_cases[i].elapsed >= sleep_test_cases[i].requested,
            i == 0 ? "20-tick sleep respects deadline" :
            i == 1 ? "50-tick sleep respects deadline" :
                     "100-tick sleep respects deadline");

        sleep_test_check(
            sleep_test_cases[i].cpu_before == cpu &&
            sleep_test_cases[i].cpu_after == cpu,
            "sleeper remains on assigned CPU");
    }

done:
    print("Sleep tests: ");
    print_uint(sleep_test_checks);
    print(" checks, ");
    print_uint(sleep_test_fails);
    print(" failed\n");

    return sleep_test_fails == 0;
}

static uint64_t sleep_test_read_rflags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static int sleep_test_race_run(void) {
    enum { ROUNDS = 32, WATCHDOG_SPINS = 5000000u };

    static struct sleep_test_case cases[ROUNDS][SLEEP_TEST_CASES];

    sleep_test_checks = 0;
    sleep_test_fails = 0;

    print("Sleep race stress test\n");

    uint32_t cpu = percpu_self()->cpu_id;
    struct percpu *target = percpu_by_cpu_id(cpu);
    uint32_t bad = 0;
    int timed_out = 0;
    int stalled = 0;

    if (target == 0) {
        sleep_test_check(0, "target CPU exists");
        goto report;
    }

    print("CPU=");
    print_uint(cpu);
    print(" target=");
    print_uint((uint32_t)(percpu_self() == target));
    print(" ticks=");
    print_uint((uint32_t)__atomic_load_n(&target->ticks, __ATOMIC_RELAXED));
    print(" IF=");
    print_uint((uint32_t)((sleep_test_read_rflags() >> 9) & 1));
    print("\n");

    for (uint32_t round = 0; round < ROUNDS; round++) {
        uint8_t launched[SLEEP_TEST_CASES] = {0};
        uint32_t created = 0;

        __atomic_store_n(&sleep_test_done, 0, __ATOMIC_RELEASE);

        for (uint32_t i = 0; i < SLEEP_TEST_CASES; i++) {
            struct sleep_test_case *c = &cases[round][i];

            c->requested = 12u + ((round * 5u + i * 3u) % 12u);
            c->elapsed = 0;
            c->cpu_before = UINT32_MAX;
            c->cpu_after = UINT32_MAX;

            if (thread_create_on("sleeprace", sleep_test_worker, c, cpu)) {
                launched[i] = 1;
                created++;
            } else {
                bad++;
            }
        }

        uint64_t start = __atomic_load_n(&target->ticks, __ATOMIC_RELAXED);
        uint64_t now = start;
        uint32_t spins = 0;

        if (round == 0) {
            print("Wait: done=");
            print_uint(__atomic_load_n(&sleep_test_done, __ATOMIC_ACQUIRE));
            print("/");
            print_uint(created);
            print(" tick=");
            print_uint((uint32_t)start);
            print("\n");
        }

        while (__atomic_load_n(&sleep_test_done, __ATOMIC_ACQUIRE) < created) {
            now = __atomic_load_n(&target->ticks, __ATOMIC_RELAXED);

            if (now - start >= SLEEP_TEST_TIMEOUT) {
                timed_out = 1;
                break;
            }

            if (++spins >= WATCHDOG_SPINS) {
                timed_out = 1;
                stalled = (now == start);
                break;
            }

            sched_yield();
        }

        if (timed_out) {
            print("TIMEOUT round=");
            print_uint(round);
            print(" done=");
            print_uint(__atomic_load_n(&sleep_test_done, __ATOMIC_ACQUIRE));
            print("/");
            print_uint(created);
            print(" tick=");
            print_uint((uint32_t)start);
            print("->");
            print_uint((uint32_t)now);
            print("\nIF=");
            print_uint((uint32_t)((sleep_test_read_rflags() >> 9) & 1));
            print(" self=");
            print_uint((uint32_t)(percpu_self() == target));
            print(" selfcpu=");
            print_uint(percpu_self()->cpu_id);
            print(" selftick=");
            print_uint((uint32_t)__atomic_load_n(
                &percpu_self()->ticks, __ATOMIC_RELAXED));
            print("\n");
            break;
        }

        for (uint32_t i = 0; i < SLEEP_TEST_CASES; i++) {
            if (!launched[i])
                continue;

            struct sleep_test_case *c = &cases[round][i];

            if (c->elapsed < c->requested)
                bad++;

            if (c->cpu_before != cpu || c->cpu_after != cpu)
                bad++;
        }

        print(".");
    }

    print("\n");

    if (stalled)
        print("Watchdog: ticks unchanged\n");

    sleep_test_check(!timed_out, "all concurrent sleepers completed");
    sleep_test_check(bad == 0, "no early wakeups, CPU mismatches, or creation failures");

report:
    print("Sleep race: ");
    print_uint(sleep_test_checks);
    print(" checks, ");
    print_uint(sleep_test_fails);
    print(" failed\n");

    return sleep_test_fails == 0;
}
#endif
