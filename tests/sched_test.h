#ifndef SCHED_TEST_H
#define SCHED_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"
#include "../include/percpu.h"
#include "../include/buddy.h"
#include "../include/kmalloc.h"
#include "../include/thread.h"
#include "../include/sched.h"
#include "../include/spinlock.h"
#include "../include/wait_queue.h"

#define ST_MAX_REPORTS 10u
#define ST_TIMEOUT_TICKS 1500u
#define ST_PP_TIMEOUT_TICKS 3000u
#define ST_MAX_CPUS 64u
#define ST_COUNT_THREADS 8u
#define ST_COUNT_ITERS 1000u
#define ST_ALT_ITERS 200u
#define ST_BURST_THREADS 64u
#define ST_PINGPONG_ITERS 10000u
#define ST_WAITERS 6u
#define ST_STRESS_THREADS 8u
#define ST_STRESS_ITERS 1500u
#define ST_SLOTS 16u

#define ST_CHECK(cond, name) do { st_checks++; if (!(cond)) { st_fails++; if (st_fails <= ST_MAX_REPORTS) { print("FAIL "); print(name); print("\n"); } } } while (0)

typedef int (*st_cond_fn)(void *ctx);

struct st_turn_ctx {
    uint32_t *turn;
    uint32_t want;
};

struct st_pp_arg {
    struct wait_queue *mine;
    struct wait_queue *other;
    struct st_turn_ctx ctx;
    uint32_t *turn;
    uint32_t *count;
    uint32_t *bad;
    uint32_t *done;
    uint32_t side;
};

static uint64_t st_checks;
static uint64_t st_fails;
static uint32_t st_done;
static uint32_t st_bad;
static uint32_t st_ready;
static uint32_t st_go;
static uint32_t st_stop;
static uint32_t st_counter;
static uint64_t st_cpu_mask;
static uint32_t st_ids[ST_COUNT_THREADS];
static uint32_t st_flags[ST_MAX_CPUS];
static uint32_t st_order_len;
static uint8_t st_order[2 * ST_ALT_ITERS];
static uint32_t st_spin_a;
static uint32_t st_spin_b;
static struct wait_queue st_queues[2][2];
static uint32_t st_pp_turn[2];
static uint32_t st_pp_count[2];
static uint32_t st_pp_bad[2];
static uint32_t st_pp_done[2];
static struct st_pp_arg st_pp_args[2][2];
static struct wait_queue st_sem_q;
static struct wait_queue st_gate_q;
static uint32_t st_sem;
static uint32_t st_gate;

static uint32_t st_load(uint32_t *v) {
    return __atomic_load_n(v, __ATOMIC_ACQUIRE);
}

static void st_store(uint32_t *v, uint32_t x) {
    __atomic_store_n(v, x, __ATOMIC_RELEASE);
}

static uint32_t st_add(uint32_t *v, uint32_t x) {
    return __atomic_fetch_add(v, x, __ATOMIC_ACQ_REL);
}

static uint64_t st_now(void) {
    return percpu_self()->ticks;
}

static uint32_t st_cpu(void) {
    return percpu_self()->cpu_id;
}

static int st_wait_ge(uint32_t *v, uint32_t target, uint32_t timeout) {
    uint64_t start = st_now();
    while (st_load(v) < target) {
        if (st_now() - start > timeout)
            return 0;
        sched_yield();
    }
    return 1;
}

static void st_settle(uint32_t ticks) {
    uint64_t start = st_now();
    while (st_now() - start < ticks)
        sched_yield();
}

static int st_wait_pages(uint64_t pages) {
    uint64_t start = st_now();
    while (buddy_available_pages() != pages) {
        if (st_now() - start > ST_TIMEOUT_TICKS)
            return 0;
        sched_yield();
    }
    return 1;
}

static void st_fill(void *ptr, size_t n, uint8_t seed) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(seed + i * 31u + (i >> 8));
}

static int st_verify(const void *ptr, size_t n, uint8_t seed) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != (uint8_t)(seed + i * 31u + (i >> 8)))
            return 0;
    }
    return 1;
}

static void st_warm_kmalloc(void) {
    static const size_t classes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    for (uint32_t i = 0; i < 8; i++) {
        void *p = kmalloc(classes[i]);
        kfree(p);
    }
}

static void st_report(const char *name, uint64_t fails_before) {
    print("[test] ");
    print(name);
    print(st_fails == fails_before ? " ok\n" : " FAILED\n");
}

static int st_stack_ok(struct thread *t) {
    uint64_t sp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(sp));
    uint64_t base = (uint64_t)(uintptr_t)t->stack_base;
    uint64_t top = base + t->stack_size;
    if (sp <= base || sp >= top)
        return 0;
    if (*(uint64_t *)t->stack_base != THREAD_CANARY)
        return 0;
    return percpu_self()->tss.rsp[0] == (top & ~0xFull);
}

static int st_wait_on(struct wait_queue *q, st_cond_fn cond, void *ctx) {
    struct wait_queue_entry entry;
    for (;;) {
        uint64_t flags;
        if (!wait_queue_prepare_to_wait(q, &entry, &flags))
            return 0;
        if (cond(ctx)) {
            sched_cancel_block_current();
            wait_queue_remove(q, &entry);
            irq_restore(flags);
            return 1;
        }
        schedule();
        wait_queue_remove(q, &entry);
        irq_restore(flags);
    }
}

static int st_cond_turn(void *ctx) {
    struct st_turn_ctx *c = (struct st_turn_ctx *)ctx;
    return st_load(c->turn) == c->want;
}

static int st_cond_sem(void *ctx) {
    (void)ctx;
    uint32_t c = st_load(&st_sem);
    while (c > 0) {
        if (__atomic_compare_exchange_n(&st_sem, &c, c - 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return 1;
    }
    return 0;
}

static int st_cond_gate(void *ctx) {
    (void)ctx;
    return st_load(&st_gate) != 0;
}

static void st_short_thread(void *arg) {
    (void)arg;
    st_add(&st_done, 1);
}

static void st_basic_thread(void *arg) {
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    struct thread *t = thread_current();
    uint32_t bad = 0;
    if (t->state != THREAD_RUNNING)
        bad++;
    if (t->name[0] == '\0')
        bad++;
    st_ids[idx] = t->id;
    for (uint32_t i = 0; i < ST_COUNT_ITERS; i++) {
        __atomic_fetch_add(&st_counter, 1, __ATOMIC_RELAXED);
        if ((i % 50) == 0) {
            if (!st_stack_ok(t))
                bad++;
            sched_yield();
        }
    }
    __atomic_fetch_or(&st_cpu_mask, 1ull << (st_cpu() & 63u), __ATOMIC_RELAXED);
    if (bad)
        st_add(&st_bad, bad);
    st_add(&st_done, 1);
}

static void st_alt_thread(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    st_add(&st_ready, 1);
    while (!st_load(&st_go))
        sched_yield();
    for (uint32_t i = 0; i < ST_ALT_ITERS; i++) {
        uint32_t idx = st_add(&st_order_len, 1);
        if (idx < 2 * ST_ALT_ITERS)
            st_order[idx] = (uint8_t)id;
        sched_yield();
    }
    st_add(&st_done, 1);
}

static void st_spin_thread(void *arg) {
    uint32_t *counter = (uint32_t *)arg;
    while (!st_load(&st_stop))
        __atomic_fetch_add(counter, 1, __ATOMIC_RELAXED);
    st_add(&st_done, 1);
}

static void st_cov_thread(void *arg) {
    uint32_t want = (uint32_t)(uintptr_t)arg;
    st_store(&st_flags[want], st_cpu() == want ? 1u : 2u);
}

static void st_pp_thread(void *arg) {
    struct st_pp_arg *a = (struct st_pp_arg *)arg;
    for (uint32_t i = 0; i < ST_PINGPONG_ITERS; i++) {
        if (!st_wait_on(a->mine, st_cond_turn, &a->ctx)) {
            st_add(a->bad, 1);
            break;
        }
        if (st_load(a->turn) != a->side)
            st_add(a->bad, 1);
        st_add(a->count, 1);
        st_store(a->turn, a->side ^ 1u);
        wait_queue_wake_one(a->other);
    }
    st_add(a->done, 1);
}

static void st_sem_thread(void *arg) {
    (void)arg;
    if (!st_wait_on(&st_sem_q, st_cond_sem, 0))
        st_add(&st_bad, 1);
    st_add(&st_done, 1);
}

static void st_gate_thread(void *arg) {
    (void)arg;
    if (!st_wait_on(&st_gate_q, st_cond_gate, 0))
        st_add(&st_bad, 1);
    st_add(&st_done, 1);
}

static void st_stress_thread(void *arg) {
    uint64_t rng = ((uint64_t)(uintptr_t)arg + 1) * 0x9E3779B97F4A7C15ull;
    void *ptr[ST_SLOTS];
    uint32_t len[ST_SLOTS];
    uint8_t seed[ST_SLOTS];
    uint32_t bad = 0;
    for (uint32_t i = 0; i < ST_SLOTS; i++)
        ptr[i] = NULL;
    for (uint32_t it = 0; it < ST_STRESS_ITERS; it++) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        uint32_t s = (uint32_t)(rng % ST_SLOTS);
        if (ptr[s]) {
            if (!st_verify(ptr[s], len[s], seed[s]))
                bad++;
            kfree(ptr[s]);
            ptr[s] = NULL;
        } else {
            uint32_t r = (uint32_t)(rng >> 20);
            uint32_t size = (r % 16u == 0) ? 2049u + (r >> 4) % 18000u : 1u + (r >> 4) % 2000u;
            void *p = kmalloc(size);
            if (!p) {
                bad++;
                continue;
            }
            seed[s] = (uint8_t)(rng >> 40);
            len[s] = size;
            ptr[s] = p;
            st_fill(p, size, seed[s]);
        }
        if ((it & 63u) == 0)
            sched_yield();
    }
    for (uint32_t i = 0; i < ST_SLOTS; i++) {
        if (!ptr[i])
            continue;
        if (!st_verify(ptr[i], len[i], seed[i]))
            bad++;
        kfree(ptr[i]);
    }
    if (bad)
        st_add(&st_bad, bad);
    st_add(&st_done, 1);
}

static void st_test_threads(void) {
    uint32_t cpus = percpu_count();
    ST_CHECK(thread_create("x", NULL, NULL) == NULL, "create rejects null function");
    st_store(&st_done, 0);
    st_store(&st_bad, 0);
    st_store(&st_counter, 0);
    __atomic_store_n(&st_cpu_mask, 0, __ATOMIC_RELEASE);
    int created = 1;
    for (uint32_t i = 0; i < ST_COUNT_THREADS; i++) {
        st_ids[i] = 0;
        if (thread_create("basic", st_basic_thread, (void *)(uintptr_t)i) == NULL)
            created = 0;
    }
    ST_CHECK(created, "threads created");
    ST_CHECK(st_wait_ge(&st_done, ST_COUNT_THREADS, ST_TIMEOUT_TICKS), "threads finished");
    ST_CHECK(st_load(&st_counter) == ST_COUNT_THREADS * ST_COUNT_ITERS, "shared counter total");
    ST_CHECK(st_load(&st_bad) == 0, "state, stack, canary and rsp0 invariants");
    int unique = 1;
    for (uint32_t i = 0; i < ST_COUNT_THREADS; i++) {
        if (st_ids[i] == 0)
            unique = 0;
        for (uint32_t j = i + 1; j < ST_COUNT_THREADS; j++) {
            if (st_ids[i] == st_ids[j])
                unique = 0;
        }
    }
    ST_CHECK(unique, "thread ids unique");
    if (cpus > 1) {
        uint64_t mask = __atomic_load_n(&st_cpu_mask, __ATOMIC_ACQUIRE);
        ST_CHECK((mask & (mask - 1)) != 0, "threads spread over cores");
    }
}

static void st_test_alternation(void) {
    uint32_t cpus = percpu_count();
    uint32_t main_cpu = st_cpu();
    uint32_t target = cpus > 1 ? (main_cpu + 1) % cpus : main_cpu;
    st_store(&st_ready, 0);
    st_store(&st_go, 0);
    st_store(&st_done, 0);
    st_store(&st_order_len, 0);
    ST_CHECK(thread_create_on("altA", st_alt_thread, (void *)1, target) != NULL, "alt thread A created");
    ST_CHECK(thread_create_on("altB", st_alt_thread, (void *)2, target) != NULL, "alt thread B created");
    ST_CHECK(st_wait_ge(&st_ready, 2, ST_TIMEOUT_TICKS), "alt threads started");
    st_settle(2);
    st_store(&st_go, 1);
    ST_CHECK(st_wait_ge(&st_done, 2, ST_TIMEOUT_TICKS), "alt threads finished");
    uint32_t len = st_load(&st_order_len);
    ST_CHECK(len == 2 * ST_ALT_ITERS, "alt record count");
    uint32_t violations = 0;
    for (uint32_t i = 1; i < len && i < 2 * ST_ALT_ITERS; i++) {
        if (st_order[i] == st_order[i - 1])
            violations++;
    }
    ST_CHECK(violations == 0, "yielding threads on one core alternate strictly");
}

static void st_test_preemption(void) {
    uint32_t cpus = percpu_count();
    if (cpus < 2) {
        print("[test] preemption skipped, needs 2 cpus\n");
        return;
    }
    uint32_t spin_cpu = (st_cpu() + 1) % cpus;
    st_store(&st_stop, 0);
    st_store(&st_spin_a, 0);
    st_store(&st_spin_b, 0);
    st_store(&st_done, 0);
    ST_CHECK(thread_create_on("spinA", st_spin_thread, &st_spin_a, spin_cpu) != NULL, "spinner A created");
    ST_CHECK(thread_create_on("spinB", st_spin_thread, &st_spin_b, spin_cpu) != NULL, "spinner B created");
    ST_CHECK(st_wait_ge(&st_spin_a, 1000, ST_TIMEOUT_TICKS), "spinner A ran");
    ST_CHECK(st_wait_ge(&st_spin_b, 1000, ST_TIMEOUT_TICKS), "spinner B ran after A without yielding");
    uint32_t a1 = st_load(&st_spin_a);
    uint32_t b1 = st_load(&st_spin_b);
    st_settle(5);
    ST_CHECK(st_load(&st_spin_a) != a1 && st_load(&st_spin_b) != b1, "both spinners keep advancing");
    st_store(&st_stop, 1);
    ST_CHECK(st_wait_ge(&st_done, 2, ST_TIMEOUT_TICKS), "spinners exited");
}

static void st_test_coverage(void) {
    uint32_t cpus = percpu_count();
    if (cpus > ST_MAX_CPUS)
        cpus = ST_MAX_CPUS;
    for (uint32_t c = 0; c < cpus; c++) {
        st_store(&st_flags[c], 0);
        ST_CHECK(thread_create_on("cov", st_cov_thread, (void *)(uintptr_t)c, c) != NULL, "pinned thread created");
    }
    for (uint32_t c = 0; c < cpus; c++) {
        ST_CHECK(st_wait_ge(&st_flags[c], 1, ST_TIMEOUT_TICKS), "pinned thread ran");
        ST_CHECK(st_load(&st_flags[c]) == 1, "pinned thread ran on its core");
    }
}

static void st_test_reap(void) {
    st_warm_kmalloc();
    st_store(&st_done, 0);
    ST_CHECK(thread_create("warm", st_short_thread, NULL) != NULL, "warmup thread created");
    ST_CHECK(st_wait_ge(&st_done, 1, ST_TIMEOUT_TICKS), "warmup thread finished");
    st_settle(5);
    uint64_t base = buddy_available_pages();
    ST_CHECK(thread_create_on("bad", st_short_thread, NULL, 0xFFFFu) == NULL, "create on bad core rejected");
    ST_CHECK(buddy_available_pages() == base, "failed create leaks nothing");
    st_store(&st_done, 0);
    int created = 1;
    for (uint32_t i = 0; i < ST_BURST_THREADS; i++) {
        if (thread_create("burst", st_short_thread, NULL) == NULL)
            created = 0;
    }
    ST_CHECK(created, "burst created");
    ST_CHECK(st_wait_ge(&st_done, ST_BURST_THREADS, ST_TIMEOUT_TICKS), "burst finished");
    st_settle(5);
    ST_CHECK(st_wait_pages(base), "exited threads fully reaped");
}

static void st_run_pingpong(uint32_t cpu_a, uint32_t cpu_b, uint32_t slot, const char *name) {
    wait_queue_init(&st_queues[slot][0]);
    wait_queue_init(&st_queues[slot][1]);
    st_store(&st_pp_turn[slot], 0);
    st_store(&st_pp_count[slot], 0);
    st_store(&st_pp_bad[slot], 0);
    st_store(&st_pp_done[slot], 0);
    for (uint32_t side = 0; side < 2; side++) {
        struct st_pp_arg *a = &st_pp_args[slot][side];
        a->mine = &st_queues[slot][side];
        a->other = &st_queues[slot][side ^ 1u];
        a->turn = &st_pp_turn[slot];
        a->count = &st_pp_count[slot];
        a->bad = &st_pp_bad[slot];
        a->done = &st_pp_done[slot];
        a->side = side;
        a->ctx.turn = &st_pp_turn[slot];
        a->ctx.want = side;
    }
    ST_CHECK(thread_create_on("ppA", st_pp_thread, &st_pp_args[slot][0], cpu_a) != NULL, name);
    ST_CHECK(thread_create_on("ppB", st_pp_thread, &st_pp_args[slot][1], cpu_b) != NULL, name);
    ST_CHECK(st_wait_ge(&st_pp_done[slot], 2, ST_PP_TIMEOUT_TICKS), "ping-pong finished, a hang means a lost wakeup");
    ST_CHECK(st_load(&st_pp_count[slot]) == 2 * ST_PINGPONG_ITERS, "ping-pong handoff count");
    ST_CHECK(st_load(&st_pp_bad[slot]) == 0, "ping-pong turn order intact");
}

static void st_test_pingpong(void) {
    uint32_t cpus = percpu_count();
    uint32_t main_cpu = st_cpu();
    uint32_t other = cpus > 1 ? (main_cpu + 1) % cpus : main_cpu;
    st_run_pingpong(other, other, 0, "same-core ping-pong threads created");
    if (cpus > 1) {
        uint32_t third = (other + 1) % cpus;
        st_run_pingpong(other, third, 1, "cross-core ping-pong threads created");
    } else {
        print("[test] cross-core ping-pong skipped, needs 2 cpus\n");
    }
}

static void st_test_wake_one(void) {
    wait_queue_init(&st_sem_q);
    st_store(&st_sem, 0);
    st_store(&st_done, 0);
    st_store(&st_bad, 0);
    int created = 1;
    for (uint32_t i = 0; i < ST_WAITERS; i++) {
        if (thread_create("semwait", st_sem_thread, NULL) == NULL)
            created = 0;
    }
    ST_CHECK(created, "semaphore waiters created");
    st_settle(5);
    ST_CHECK(st_load(&st_done) == 0, "waiters block with no permits");
    for (uint32_t k = 0; k < ST_WAITERS / 2; k++) {
        st_add(&st_sem, 1);
        wait_queue_wake_one(&st_sem_q);
        ST_CHECK(st_wait_ge(&st_done, k + 1, ST_TIMEOUT_TICKS), "each permit releases a waiter");
    }
    st_settle(5);
    ST_CHECK(st_load(&st_done) == ST_WAITERS / 2, "no more waiters than permits released");
    for (uint32_t k = ST_WAITERS / 2; k < ST_WAITERS; k++) {
        st_add(&st_sem, 1);
        wait_queue_wake_one(&st_sem_q);
    }
    ST_CHECK(st_wait_ge(&st_done, ST_WAITERS, ST_TIMEOUT_TICKS), "remaining permits release the rest");
    ST_CHECK(st_load(&st_bad) == 0, "no waiter failed to block");
}

static void st_test_wake_all(void) {
    wait_queue_init(&st_gate_q);
    st_store(&st_gate, 0);
    st_store(&st_done, 0);
    st_store(&st_bad, 0);
    int created = 1;
    for (uint32_t i = 0; i < ST_WAITERS; i++) {
        if (thread_create("gatewait", st_gate_thread, NULL) == NULL)
            created = 0;
    }
    ST_CHECK(created, "gate waiters created");
    st_settle(5);
    ST_CHECK(st_load(&st_done) == 0, "waiters block while the gate is closed");
    st_store(&st_gate, 1);
    while (wait_queue_wake_one(&st_gate_q))
        ;
    ST_CHECK(st_wait_ge(&st_done, ST_WAITERS, ST_TIMEOUT_TICKS), "opening the gate releases every waiter");
    ST_CHECK(st_load(&st_bad) == 0, "no gate waiter failed to block");
}

static void st_test_stress(void) {
    st_warm_kmalloc();
    st_settle(5);
    uint64_t base = buddy_available_pages();
    st_store(&st_done, 0);
    st_store(&st_bad, 0);
    int created = 1;
    for (uint32_t i = 0; i < ST_STRESS_THREADS; i++) {
        if (thread_create("stress", st_stress_thread, (void *)(uintptr_t)i) == NULL)
            created = 0;
    }
    ST_CHECK(created, "stress threads created");
    ST_CHECK(st_wait_ge(&st_done, ST_STRESS_THREADS, ST_TIMEOUT_TICKS), "stress threads finished");
    ST_CHECK(st_load(&st_bad) == 0, "allocations stayed intact under preemption");
    st_settle(5);
    ST_CHECK(st_wait_pages(base), "stress leaked nothing");
}

static inline int sched_test_run(void) {
    st_checks = 0;
    st_fails = 0;
    print("Running scheduler tests...\n");
    uint64_t mark = st_fails;
    st_test_threads();
    st_report("threads", mark);
    mark = st_fails;
    st_test_alternation();
    st_report("round robin", mark);
    mark = st_fails;
    st_test_preemption();
    st_report("preemption", mark);
    mark = st_fails;
    st_test_coverage();
    st_report("per-core placement", mark);
    mark = st_fails;
    st_test_reap();
    st_report("exit and reap", mark);
    mark = st_fails;
    st_test_pingpong();
    st_report("wait queue ping-pong", mark);
    mark = st_fails;
    st_test_wake_one();
    st_report("wait queue wake one", mark);
    mark = st_fails;
    st_test_wake_all();
    st_report("wait queue wake all", mark);
    mark = st_fails;
    st_test_stress();
    st_report("kmalloc under preemption", mark);
    print("Scheduler tests: ");
    print_uint(st_checks);
    print(" checks, ");
    print_uint(st_fails);
    print(" failed\n");
    return st_fails == 0;
}

#endif
