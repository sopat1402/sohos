#ifndef MEM_TEST_H
#define MEM_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"
#include "../include/phys.h"
#include "../include/buddy.h"
#include "../include/slub.h"
#include "../include/kmalloc.h"
#include "../include/spinlock.h"

#define MT_PAGE 4096ull
#define MT_MAGIC 0xC0FFEE1234567890ull
#define MT_MAX_REPORTS 10u
#define MT_DRAIN_CAP (1ull << 18)
#define MT_OBJS 2048u
#define MT_SLOTS 512u
#define MT_STRESS_ITERS 20000u
#define MT_BUDDY_SLOTS 256u
#define MT_BUDDY_ITERS 4000u
#define MT_LEAK_SLACK 64u

#define MT_CHECK(cond, name) do { mt_checks++; if (!(cond)) { mt_fails++; if (mt_fails <= MT_MAX_REPORTS) { print("FAIL "); print(name); print("\n"); } } } while (0)

static uint64_t mt_checks;
static uint64_t mt_fails;
static uint64_t mt_rng = 0x9E3779B97F4A7C15ull;
static uint64_t mt_irq_orig;
static uint64_t mt_pages[512];
static uint64_t mt_bphys[MT_BUDDY_SLOTS];
static uint8_t mt_bord[MT_BUDDY_SLOTS];
static void *mt_objs[MT_OBJS];
static uint16_t mt_perm[MT_OBJS];
static void *mt_ptr[MT_SLOTS];
static uint32_t mt_len[MT_SLOTS];
static uint8_t mt_seed[MT_SLOTS];
static uint8_t mt_slab_buf[4096] __attribute__((aligned(64)));
static slab_t mt_slab;
static slub_cache_t mt_caches[6];

static uint64_t mt_rand(void) {
    uint64_t x = mt_rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    mt_rng = x;
    return x;
}

static uint64_t mt_rflags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags) : : "memory");
    return flags;
}

static void mt_make_perm(uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        mt_perm[i] = (uint16_t)i;
    for (uint32_t i = n; i > 1; i--) {
        uint32_t j = (uint32_t)(mt_rand() % i);
        uint16_t tmp = mt_perm[i - 1];
        mt_perm[i - 1] = mt_perm[j];
        mt_perm[j] = tmp;
    }
}

static void mt_fill(void *ptr, size_t n, uint8_t seed) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(seed + i * 31u + (i >> 8));
}

static int mt_verify(const void *ptr, size_t n, uint8_t seed) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != (uint8_t)(seed + i * 31u + (i >> 8)))
            return 0;
    }
    return 1;
}

static int mt_all(const void *ptr, size_t n, uint8_t value) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != value)
            return 0;
    }
    return 1;
}

static int mt_block_zero(uint64_t phys, uint8_t order, int full) {
    volatile uint64_t *w = (volatile uint64_t *)phys_to_virt(phys);
    uint64_t count = (MT_PAGE << order) / 8;
    uint64_t step = full ? 1 : 61;
    for (uint64_t i = 0; i < count; i += step) {
        if (w[i])
            return 0;
    }
    return w[count - 1] == 0;
}

static void mt_block_dirty(uint64_t phys, uint8_t order, int full) {
    volatile uint64_t *w = (volatile uint64_t *)phys_to_virt(phys);
    uint64_t count = (MT_PAGE << order) / 8;
    uint64_t step = full ? 1 : 61;
    for (uint64_t i = 0; i < count; i += step)
        w[i] = ~0ull;
    w[count - 1] = ~0ull;
}

static void mt_report(const char *name, uint64_t fails_before) {
    print("[test] ");
    print(name);
    print(mt_fails == fails_before ? " ok\n" : " FAILED\n");
}

static void mt_test_spinlock(void) {
    spinlock_t lock = SPINLOCK_INIT;
    uint64_t original = mt_rflags() & RFLAGS_IF;
    spin_lock(&lock);
    MT_CHECK(atomic_flag_test_and_set_explicit(&lock.flag, memory_order_acquire) == 1, "lock is held");
    spin_unlock(&lock);
    MT_CHECK(atomic_flag_test_and_set_explicit(&lock.flag, memory_order_acquire) == 0, "lock is free");
    atomic_flag_clear(&lock.flag);
    uint64_t outer = spin_lock_irqsave(&lock);
    MT_CHECK((mt_rflags() & RFLAGS_IF) == 0, "irqs off inside lock");
    MT_CHECK((outer & RFLAGS_IF) == original, "saved flags match");
    spinlock_t inner_lock = SPINLOCK_INIT;
    uint64_t inner = spin_lock_irqsave(&inner_lock);
    MT_CHECK((inner & RFLAGS_IF) == 0, "nested save sees irqs off");
    spin_unlock_irqrestore(&inner_lock, inner);
    MT_CHECK((mt_rflags() & RFLAGS_IF) == 0, "nested restore keeps irqs off");
    spin_unlock_irqrestore(&lock, outer);
    MT_CHECK((mt_rflags() & RFLAGS_IF) == original, "outer restore");
}

static void mt_buddy_basic(uint64_t base) {
    uint64_t p = buddy_alloc_order(0);
    MT_CHECK(p != 0 && (p & (MT_PAGE - 1)) == 0, "alloc order 0 aligned");
    if (p) {
        MT_CHECK(buddy_available_pages() == base - 1, "alloc order 0 count");
        MT_CHECK(mt_block_zero(p, 0, 1), "alloc order 0 zeroed");
        mt_block_dirty(p, 0, 1);
        MT_CHECK(buddy_free_order(p, 0) == 1, "free order 0");
        MT_CHECK(buddy_available_pages() == base, "free order 0 count");
        MT_CHECK(buddy_free_order(p, 0) == 0, "double free rejected");
        MT_CHECK(buddy_available_pages() == base, "double free changed nothing");
        uint64_t q = buddy_alloc_order(0);
        MT_CHECK(q != 0 && mt_block_zero(q, 0, 1), "reallocated page zeroed");
        if (q)
            buddy_free_order(q, 0);
    }
    uint64_t a = buddy_alloc_order(3);
    if (a) {
        mt_block_dirty(a, 3, 1);
        buddy_free_order(a, 3);
        uint64_t b = buddy_alloc_order(3);
        MT_CHECK(b != 0 && mt_block_zero(b, 3, 1), "reallocated order 3 zeroed");
        if (b)
            buddy_free_order(b, 3);
    }
    MT_CHECK(buddy_available_pages() == base, "basic section restored");
}

static uint8_t mt_buddy_orders(uint64_t base) {
    uint8_t top = 0;
    for (uint8_t o = 0; o <= 16; o++) {
        uint64_t b = buddy_alloc_order(o);
        if (!b)
            break;
        top = o;
        MT_CHECK((b & ((MT_PAGE << o) - 1)) == 0, "block aligned to its size");
        MT_CHECK(buddy_available_pages() == base - (1ull << o), "order alloc count");
        MT_CHECK(mt_block_zero(b, o, o <= 6), "order block zeroed");
        mt_block_dirty(b, o, o <= 6);
        MT_CHECK(buddy_free_order(b, o) == 1, "order free");
        MT_CHECK(buddy_available_pages() == base, "order restored");
    }
    MT_CHECK(top >= 4, "orders up to 4 allocatable");
    return top;
}

static void mt_buddy_invalid(uint64_t base) {
    uint64_t b = buddy_alloc_order(2);
    MT_CHECK(b != 0, "order 2 alloc");
    if (b) {
        MT_CHECK(buddy_free_order(b, 1) == 0, "free with lower order rejected");
        MT_CHECK(buddy_free_order(b, 3) == 0, "free with higher order rejected");
        MT_CHECK(buddy_free_order(b + MT_PAGE, 0) == 0, "free of interior page rejected");
        MT_CHECK(buddy_free_order(b + 1, 2) == 0, "misaligned free rejected");
        MT_CHECK(buddy_available_pages() == base - 4, "rejected frees changed nothing");
        MT_CHECK(buddy_free_order(b, 2) == 1, "correct free");
        MT_CHECK(buddy_free_order(b, 2) == 0, "second free rejected");
    }
    MT_CHECK(buddy_free_order(0, 0) == 0, "free of zero rejected");
    MT_CHECK(buddy_free_order(MT_PAGE * 3, 99) == 0, "free with absurd order rejected");
    MT_CHECK(buddy_alloc_order(60) == 0, "alloc order 60 rejected");
    MT_CHECK(buddy_alloc_order(255) == 0, "alloc order 255 rejected");
    MT_CHECK(buddy_available_pages() == base, "invalid section restored");
}

static void mt_buddy_overlap(uint64_t base) {
    uint32_t n = 256;
    uint32_t got = 0;
    int zeroed = 1;
    for (; got < n; got++) {
        uint64_t pg = buddy_alloc_order(0);
        if (!pg)
            break;
        if (!mt_block_zero(pg, 0, 0))
            zeroed = 0;
        volatile uint64_t *w = (volatile uint64_t *)phys_to_virt(pg);
        w[0] = got ^ MT_MAGIC;
        w[511] = (got * 3ull) ^ MT_MAGIC;
        mt_pages[got] = pg;
    }
    MT_CHECK(got == n, "256 pages allocated");
    MT_CHECK(zeroed, "all pages zeroed");
    MT_CHECK(buddy_available_pages() == base - got, "256 pages counted");
    int intact = 1;
    int distinct = 1;
    for (uint32_t i = 0; i < got; i++) {
        volatile uint64_t *w = (volatile uint64_t *)phys_to_virt(mt_pages[i]);
        if (w[0] != (i ^ MT_MAGIC) || w[511] != ((i * 3ull) ^ MT_MAGIC))
            intact = 0;
        for (uint32_t j = i + 1; j < got; j++) {
            if (mt_pages[i] == mt_pages[j])
                distinct = 0;
        }
    }
    MT_CHECK(intact, "page contents intact");
    MT_CHECK(distinct, "pages distinct");
    mt_make_perm(got);
    int freed = 1;
    for (uint32_t i = 0; i < got; i++) {
        if (buddy_free_order(mt_pages[mt_perm[i]], 0) != 1)
            freed = 0;
    }
    MT_CHECK(freed, "shuffled frees accepted");
    MT_CHECK(buddy_available_pages() == base, "overlap section restored");
}

static void mt_buddy_coalesce(uint64_t base, uint8_t top) {
    uint8_t k = top < 8 ? top : 8;
    uint64_t pages = 1ull << k;
    if (k < 2 || pages > 512)
        return;
    for (uint32_t round = 0; round < 3; round++) {
        uint32_t got = 0;
        for (; got < pages; got++) {
            uint64_t pg = buddy_alloc_order(0);
            if (!pg)
                break;
            mt_pages[got] = pg;
        }
        MT_CHECK(got == pages, "coalesce pages allocated");
        mt_make_perm(got);
        int freed = 1;
        for (uint32_t i = 0; i < got; i++) {
            uint32_t idx = round == 0 ? i : (round == 1 ? got - 1 - i : mt_perm[i]);
            if (buddy_free_order(mt_pages[idx], 0) != 1)
                freed = 0;
        }
        MT_CHECK(freed, "coalesce frees accepted");
        MT_CHECK(buddy_available_pages() == base, "coalesce count restored");
        uint64_t big = buddy_alloc_order(k);
        MT_CHECK(big != 0, "pages merged back into a large block");
        if (big)
            buddy_free_order(big, k);
    }
    MT_CHECK(buddy_available_pages() == base, "coalesce section restored");
}

static void mt_buddy_mixed(uint64_t base) {
    for (uint32_t i = 0; i < MT_BUDDY_SLOTS; i++)
        mt_bphys[i] = 0;
    uint32_t bad = 0;
    for (uint32_t it = 0; it < MT_BUDDY_ITERS; it++) {
        uint32_t s = (uint32_t)(mt_rand() % MT_BUDDY_SLOTS);
        if (!mt_bphys[s]) {
            uint8_t o = (uint8_t)(mt_rand() % 6);
            uint64_t b = buddy_alloc_order(o);
            if (!b)
                continue;
            if ((b & ((MT_PAGE << o) - 1)) != 0 || !mt_block_zero(b, o, 0))
                bad++;
            volatile uint64_t *w = (volatile uint64_t *)phys_to_virt(b);
            w[0] = b ^ MT_MAGIC;
            w[((MT_PAGE << o) / 8) - 1] = ~b ^ MT_MAGIC;
            mt_bphys[s] = b;
            mt_bord[s] = o;
        } else {
            uint64_t b = mt_bphys[s];
            uint8_t o = mt_bord[s];
            volatile uint64_t *w = (volatile uint64_t *)phys_to_virt(b);
            if (w[0] != (b ^ MT_MAGIC) || w[((MT_PAGE << o) / 8) - 1] != (~b ^ MT_MAGIC))
                bad++;
            if (buddy_free_order(b, o) != 1)
                bad++;
            mt_bphys[s] = 0;
        }
    }
    for (uint32_t i = 0; i < MT_BUDDY_SLOTS; i++) {
        if (mt_bphys[i] && buddy_free_order(mt_bphys[i], mt_bord[i]) != 1)
            bad++;
        mt_bphys[i] = 0;
    }
    MT_CHECK(bad == 0, "mixed order workload clean");
    MT_CHECK(buddy_available_pages() == base, "mixed workload restored");
}

static void mt_buddy_drain(uint64_t base) {
    uint64_t head = 0;
    uint64_t n = 0;
    while (n < MT_DRAIN_CAP) {
        uint64_t p = buddy_alloc_order(0);
        if (!p)
            break;
        *(volatile uint64_t *)phys_to_virt(p) = head;
        head = p;
        n++;
    }
    if (n < MT_DRAIN_CAP) {
        MT_CHECK(buddy_available_pages() == 0, "drained to zero");
        MT_CHECK(buddy_alloc_order(0) == 0, "alloc fails when empty");
        MT_CHECK(buddy_alloc_order(1) == 0, "second failed alloc does not hang");
        MT_CHECK(buddy_alloc_order(9) == 0, "large alloc fails when empty");
    } else {
        print("[test] drain capped, exhaustion skipped\n");
    }
    uint64_t freed = 0;
    while (head) {
        uint64_t next = *(volatile uint64_t *)phys_to_virt(head);
        if (buddy_free_order(head, 0) != 1)
            break;
        freed++;
        head = next;
    }
    MT_CHECK(freed == n, "every drained page freed");
    MT_CHECK(buddy_available_pages() == base, "drain restored");
}

static void mt_test_buddy(void) {
    uint64_t base = buddy_available_pages();
    MT_CHECK(base > 600, "enough memory to test");
    mt_buddy_basic(base);
    uint8_t top = mt_buddy_orders(base);
    mt_buddy_invalid(base);
    mt_buddy_overlap(base);
    mt_buddy_coalesce(base, top);
    mt_buddy_mixed(base);
    mt_buddy_drain(base);
    MT_CHECK((mt_rflags() & RFLAGS_IF) == mt_irq_orig, "irq state after buddy");
}

static void mt_test_slab_direct(void) {
    slub_cache_t *c = &mt_caches[0];
    MT_CHECK(slub_cache_init(NULL, 8, 8, 0) == 0, "cache init rejects null");
    MT_CHECK(slub_cache_init(c, 0, 8, 0) == 0, "cache init rejects zero size");
    MT_CHECK(slub_cache_init(c, 8, 0, 0) == 0, "cache init rejects zero alignment");
    MT_CHECK(slub_cache_init(c, 8, 3, 0) == 0, "cache init rejects odd alignment");
    MT_CHECK(slub_cache_init(c, 24, 8, 0) == 1, "cache init");
    MT_CHECK(slab_init(NULL, c, mt_slab_buf, 4096) == 0, "slab_init rejects null slab");
    MT_CHECK(slab_init(&mt_slab, NULL, mt_slab_buf, 4096) == 0, "slab_init rejects null cache");
    MT_CHECK(slab_init(&mt_slab, c, NULL, 4096) == 0, "slab_init rejects null base");
    MT_CHECK(slab_init(&mt_slab, c, mt_slab_buf, 0) == 0, "slab_init rejects zero bytes");
    slub_cache_t *big = &mt_caches[1];
    MT_CHECK(slub_cache_init(big, 5000, 8, 0) == 1, "oversize cache init");
    MT_CHECK(slab_init(&mt_slab, big, mt_slab_buf, 4096) == 0, "slab_init rejects object larger than slab");
    MT_CHECK(slab_init(&mt_slab, c, mt_slab_buf, 4096) == 1, "slab_init");
    size_t total = mt_slab.total_objects;
    MT_CHECK(total == 4096 / 24, "slab object count");
    MT_CHECK(c->slot_size == 24 && c->obj_per_slab == total, "cache geometry");
    MT_CHECK(mt_slab.free_count == total, "slab starts fully free");
    uint32_t got = 0;
    int stamped = 1;
    while (got < total + 4) {
        void *o = slab_alloc(&mt_slab);
        if (!o)
            break;
        if (((uintptr_t)o & 7) != 0)
            stamped = 0;
        *(uint64_t *)o = got ^ MT_MAGIC;
        mt_objs[got++] = o;
    }
    MT_CHECK(got == total, "slab hands out exactly total objects");
    MT_CHECK(mt_objs[0] == (void *)mt_slab_buf, "first slot at base");
    MT_CHECK(mt_objs[1] == (void *)(mt_slab_buf + 24), "slots in address order");
    MT_CHECK(stamped, "slots aligned");
    MT_CHECK(mt_slab.free_count == 0 && slab_alloc(&mt_slab) == NULL, "slab exhausted");
    int intact = 1;
    for (uint32_t i = 0; i < got; i++) {
        if (*(uint64_t *)mt_objs[i] != (i ^ MT_MAGIC))
            intact = 0;
    }
    MT_CHECK(intact, "slots do not overlap");
    MT_CHECK(slab_free(&mt_slab, mt_objs[5]) == 1, "slab_free");
    MT_CHECK(slab_free(&mt_slab, mt_objs[5]) == 0, "slab double free rejected");
    MT_CHECK(slab_free(&mt_slab, (uint8_t *)mt_objs[6] + 1) == 0, "slab misaligned free rejected");
    MT_CHECK(slab_free(&mt_slab, (void *)((uintptr_t)mt_slab_buf - 8)) == 0, "free below slab rejected");
    MT_CHECK(slab_free(&mt_slab, (void *)((uintptr_t)mt_slab_buf + 4096)) == 0, "free above slab rejected");
    MT_CHECK(slab_free(&mt_slab, NULL) == 0, "slab free null rejected");
    MT_CHECK(slab_free(NULL, mt_objs[6]) == 0, "slab free on null slab rejected");
    MT_CHECK(slab_alloc(NULL) == NULL, "slab_alloc on null slab");
    MT_CHECK(slab_alloc(&mt_slab) == mt_objs[5], "freed slot is reused");
    int freed = 1;
    for (uint32_t i = 0; i < got; i++) {
        if (slab_free(&mt_slab, mt_objs[i]) != 1)
            freed = 0;
    }
    MT_CHECK(freed, "all slots freed");
    MT_CHECK(mt_slab.free_count == total, "slab fully free again");
}

static void mt_slub_cycle(slub_cache_t *c, size_t obj, size_t al, uint8_t order, const char *tag) {
    uint64_t before = mt_fails;
    uint64_t base = buddy_available_pages();
    MT_CHECK(slub_cache_init(c, obj, al, order) == 1, "cache init");
    void *first = slub_alloc(c);
    MT_CHECK(first != NULL, "first slub alloc");
    if (!first)
        return;
    size_t per = c->obj_per_slab;
    MT_CHECK(per > 0, "objects per slab");
    MT_CHECK(c->slot_size >= obj && c->slot_size % al == 0, "slot size");
    size_t n = per * 3 + 7;
    if (n > MT_OBJS)
        n = MT_OBJS;
    mt_objs[0] = first;
    uint32_t got = 1;
    while (got < n) {
        void *o = slub_alloc(c);
        if (!o)
            break;
        mt_objs[got++] = o;
    }
    MT_CHECK(got == n, "slub allocations");
    int aligned = 1;
    for (uint32_t i = 0; i < got; i++) {
        if (((uintptr_t)mt_objs[i] % al) != 0)
            aligned = 0;
        mt_fill(mt_objs[i], obj, (uint8_t)(i * 7 + 1));
    }
    MT_CHECK(aligned, "objects aligned");
    uint64_t slabs = (got + per - 1) / per;
    MT_CHECK(buddy_available_pages() == base - (slabs << order), "slab page usage");
    int intact = 1;
    for (uint32_t i = 0; i < got; i++) {
        if (!mt_verify(mt_objs[i], obj, (uint8_t)(i * 7 + 1)))
            intact = 0;
    }
    MT_CHECK(intact, "objects do not overlap");
    MT_CHECK(slub_free(c, (uint8_t *)mt_objs[0] + 1) == 0, "interior pointer rejected");
    MT_CHECK(slub_free(c, (void *)mt_objs) == 0, "foreign pointer rejected");
    MT_CHECK(slub_free(c, NULL) == 0, "null free rejected");
    uint64_t mid = buddy_available_pages();
    int freed = 1;
    for (uint32_t i = 0; i < got; i += 2) {
        if (slub_free(c, mt_objs[i]) != 1)
            freed = 0;
    }
    MT_CHECK(freed, "even objects freed");
    intact = 1;
    for (uint32_t i = 1; i < got; i += 2) {
        if (!mt_verify(mt_objs[i], obj, (uint8_t)(i * 7 + 1)))
            intact = 0;
    }
    MT_CHECK(intact, "odd objects survive neighbour frees");
    int again = 1;
    for (uint32_t i = 0; i < got; i += 2) {
        void *o = slub_alloc(c);
        if (!o) {
            again = 0;
            continue;
        }
        mt_objs[i] = o;
        mt_fill(o, obj, (uint8_t)(i * 7 + 1));
    }
    MT_CHECK(again, "freed slots reused");
    if (per >= 8)
        MT_CHECK(buddy_available_pages() == mid, "reuse needed no new slab");
    mt_make_perm(got);
    intact = 1;
    freed = 1;
    for (uint32_t i = 0; i < got; i++) {
        uint32_t idx = mt_perm[i];
        if (!mt_verify(mt_objs[idx], obj, (uint8_t)(idx * 7 + 1)))
            intact = 0;
        if (slub_free(c, mt_objs[idx]) != 1)
            freed = 0;
    }
    MT_CHECK(intact, "objects intact before final frees");
    MT_CHECK(freed, "final frees accepted");
    MT_CHECK(buddy_available_pages() == base - (1ull << order), "empty slabs released, one kept");
    MT_CHECK(slub_free(c, mt_objs[mt_perm[got - 1]]) == 0, "double free rejected");
    void *last = slub_alloc(c);
    MT_CHECK(last != NULL, "cache usable after full release");
    if (last)
        MT_CHECK(slub_free(c, last) == 1, "free after full release");
    if (mt_fails != before) {
        print("  in cache ");
        print(tag);
        print("\n");
    }
}

static void mt_test_slub(void) {
    mt_test_slab_direct();
    mt_slub_cycle(&mt_caches[0], 24, 8, 0, "24/8/0");
    mt_slub_cycle(&mt_caches[1], 40, 64, 0, "40/64/0");
    mt_slub_cycle(&mt_caches[2], 1000, 16, 1, "1000/16/1");
    mt_slub_cycle(&mt_caches[3], 8, 8, 0, "8/8/0");
    mt_slub_cycle(&mt_caches[4], 200, 32, 0, "200/32/0");
    uint64_t base = buddy_available_pages();
    MT_CHECK(slub_cache_init(&mt_caches[5], 5000, 8, 0) == 1, "oversize cache");
    MT_CHECK(slub_alloc(&mt_caches[5]) == NULL, "oversize alloc fails");
    MT_CHECK(buddy_available_pages() == base, "failed grow leaked nothing");
    MT_CHECK(slub_alloc(NULL) == NULL, "slub_alloc null cache");
    MT_CHECK(slub_free(NULL, mt_objs) == 0, "slub_free null cache");
    MT_CHECK((mt_rflags() & RFLAGS_IF) == mt_irq_orig, "irq state after slub");
}

static void mt_kmalloc_sizes(uint64_t base) {
    static const size_t sizes[] = {1, 2, 7, 8, 15, 16, 17, 24, 31, 32, 33, 48, 63, 64, 65, 100, 127, 128, 129, 200, 255, 256, 257, 500, 511, 512, 513, 1000, 1023, 1024, 1025, 1500, 2047, 2048};
    uint32_t count = (uint32_t)(sizeof(sizes) / sizeof(sizes[0]));
    int allocated = 1;
    int sized = 1;
    int aligned = 1;
    for (uint32_t i = 0; i < count; i++) {
        void *p = kmalloc(sizes[i]);
        mt_objs[i] = p;
        if (!p) {
            allocated = 0;
            continue;
        }
        size_t ks = ksize(p);
        if (ks < sizes[i] || (ks & (ks - 1)) != 0 || (ks > 16 && ks / 2 >= sizes[i]))
            sized = 0;
        if (((uintptr_t)p % ks) != 0)
            aligned = 0;
        mt_fill(p, sizes[i], (uint8_t)(i * 5 + 3));
    }
    MT_CHECK(allocated, "small kmalloc sizes");
    MT_CHECK(sized, "small ksize classes");
    MT_CHECK(aligned, "small objects aligned to class");
    int intact = 1;
    for (uint32_t i = 0; i < count; i++) {
        if (mt_objs[i] && !mt_verify(mt_objs[i], sizes[i], (uint8_t)(i * 5 + 3)))
            intact = 0;
    }
    MT_CHECK(intact, "small objects do not overlap");
    for (uint32_t i = 0; i < count; i++)
        kfree(mt_objs[i]);
    MT_CHECK(buddy_available_pages() == base, "small sizes restored");
}

static void mt_kmalloc_large(uint64_t base) {
    static const size_t sizes[] = {2049, 4096, 4097, 8192, 10000, 65536, 100000, 1048576};
    uint32_t count = (uint32_t)(sizeof(sizes) / sizeof(sizes[0]));
    int allocated = 1;
    int sized = 1;
    int aligned = 1;
    for (uint32_t i = 0; i < count; i++) {
        if (sizes[i] > 262144 && base < 8192) {
            mt_objs[i] = NULL;
            continue;
        }
        void *p = kmalloc(sizes[i]);
        mt_objs[i] = p;
        if (!p) {
            allocated = 0;
            continue;
        }
        size_t ks = ksize(p);
        if (ks < sizes[i] || (ks & (ks - 1)) != 0 || ks / 2 >= sizes[i])
            sized = 0;
        if (((uintptr_t)p & (MT_PAGE - 1)) != 0)
            aligned = 0;
        mt_fill(p, sizes[i], (uint8_t)(i * 11 + 1));
    }
    MT_CHECK(allocated, "large kmalloc sizes");
    MT_CHECK(sized, "large ksize rounding");
    MT_CHECK(aligned, "large blocks page aligned");
    int intact = 1;
    for (uint32_t i = 0; i < count; i++) {
        if (mt_objs[i] && !mt_verify(mt_objs[i], sizes[i], (uint8_t)(i * 11 + 1)))
            intact = 0;
    }
    MT_CHECK(intact, "large blocks do not overlap");
    for (uint32_t i = 0; i < count; i++)
        kfree(mt_objs[i]);
    MT_CHECK(buddy_available_pages() == base, "large sizes restored");
}

static void mt_kmalloc_invalid(uint64_t base) {
    int local = 0;
    MT_CHECK(kmalloc(0) == NULL, "kmalloc zero");
    MT_CHECK(kmalloc((size_t)-1) == NULL, "kmalloc huge");
    MT_CHECK(kmalloc((size_t)1 << 62) == NULL, "kmalloc absurd");
    MT_CHECK(ksize(NULL) == 0, "ksize null");
    kfree(NULL);
    kfree(&local);
    void *big = kmalloc(5000);
    MT_CHECK(big != NULL, "large alloc for double free");
    if (big) {
        kfree(big);
        MT_CHECK(ksize(big) == 0, "ksize after large free");
        kfree(big);
    }
    void *p = kmalloc(64);
    MT_CHECK(p != NULL, "small alloc for double free");
    if (p) {
        kfree(p);
        kfree(p);
        void *a = kmalloc(64);
        void *b = kmalloc(64);
        MT_CHECK(a != NULL && b != NULL && a != b, "small double free not duplicated");
        kfree(a);
        kfree(b);
    }
    MT_CHECK(buddy_available_pages() == base, "invalid section restored");
}

static void mt_kmalloc_zero(uint64_t base) {
    static const size_t sizes[] = {16, 100, 2048, 5000, 70000};
    int clean = 1;
    for (uint32_t i = 0; i < 5; i++) {
        void *p = kmalloc(sizes[i]);
        if (!p) {
            clean = 0;
            continue;
        }
        for (size_t k = 0; k < ksize(p); k++)
            ((uint8_t *)p)[k] = 0xA5;
        kfree(p);
        void *q = kzalloc(sizes[i]);
        if (!q || !mt_all(q, ksize(q), 0))
            clean = 0;
        kfree(q);
    }
    MT_CHECK(clean, "kzalloc returns zeroed memory");
    MT_CHECK(buddy_available_pages() == base, "zero section restored");
}

static void mt_kmalloc_realloc(uint64_t base) {
    void *p = krealloc(NULL, 20);
    MT_CHECK(p != NULL, "krealloc null acts as kmalloc");
    if (p) {
        mt_fill(p, 20, 0x31);
        void *q = krealloc(p, 200);
        MT_CHECK(q != NULL && mt_verify(q, 20, 0x31), "krealloc grow keeps data");
        MT_CHECK(q != NULL && ksize(q) >= 200, "krealloc grow size");
        if (q) {
            mt_fill(q, 200, 0x42);
            void *r = krealloc(q, 10);
            MT_CHECK(r == q, "krealloc shrink stays in place");
            MT_CHECK(r != NULL && mt_verify(r, 10, 0x42), "krealloc shrink keeps data");
            void *s = krealloc(r, 3000);
            MT_CHECK(s != NULL && mt_verify(s, 10, 0x42), "krealloc into large keeps data");
            if (s) {
                mt_fill(s, 3000, 0x53);
                void *t = krealloc(s, 20000);
                MT_CHECK(t != NULL && mt_verify(t, 3000, 0x53), "krealloc large grow keeps data");
                MT_CHECK(krealloc(t, 0) == NULL, "krealloc zero frees");
            }
        }
    }
    MT_CHECK(buddy_available_pages() == base, "realloc section restored");
}

static uint32_t mt_pick_size(int large_ok) {
    uint64_t r = mt_rand();
    uint32_t bucket = (uint32_t)(r & 7);
    uint64_t v = r >> 8;
    if (bucket < 5)
        return (uint32_t)(1 + v % 256);
    if (bucket < 7)
        return (uint32_t)(1 + v % 2048);
    if (large_ok && (v & 15) == 0)
        return (uint32_t)(2049 + (v >> 4) % 60000);
    return (uint32_t)(1 + (v >> 4) % 4096);
}

static void mt_kmalloc_stress(uint64_t base) {
    int large_ok = base > 8192;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < MT_SLOTS; i++)
        mt_ptr[i] = NULL;
    for (uint32_t it = 0; it < MT_STRESS_ITERS; it++) {
        uint32_t s = (uint32_t)(mt_rand() % MT_SLOTS);
        if (!mt_ptr[s]) {
            uint32_t len = mt_pick_size(large_ok);
            int zero = (int)(mt_rand() & 1);
            void *p = zero ? kzalloc(len) : kmalloc(len);
            if (!p) {
                bad++;
                continue;
            }
            if (zero && !mt_all(p, len, 0))
                bad++;
            if (ksize(p) < len)
                bad++;
            mt_ptr[s] = p;
            mt_len[s] = len;
            mt_seed[s] = (uint8_t)mt_rand();
            mt_fill(p, len, mt_seed[s]);
            continue;
        }
        if (!mt_verify(mt_ptr[s], mt_len[s], mt_seed[s]))
            bad++;
        uint32_t op = (uint32_t)(mt_rand() % 3);
        if (op == 0) {
            kfree(mt_ptr[s]);
            mt_ptr[s] = NULL;
        } else if (op == 1) {
            uint32_t newlen = mt_pick_size(large_ok);
            void *np = krealloc(mt_ptr[s], newlen);
            if (!np) {
                bad++;
                continue;
            }
            uint32_t keep = newlen < mt_len[s] ? newlen : mt_len[s];
            if (!mt_verify(np, keep, mt_seed[s]))
                bad++;
            if (ksize(np) < newlen)
                bad++;
            mt_ptr[s] = np;
            mt_len[s] = newlen;
            mt_fill(np, newlen, mt_seed[s]);
        }
    }
    for (uint32_t i = 0; i < MT_SLOTS; i++) {
        if (!mt_ptr[i])
            continue;
        if (!mt_verify(mt_ptr[i], mt_len[i], mt_seed[i]))
            bad++;
        kfree(mt_ptr[i]);
        mt_ptr[i] = NULL;
    }
    MT_CHECK(bad == 0, "stress workload clean");
    MT_CHECK(buddy_available_pages() == base, "stress leaked nothing");
}

static void mt_test_kmalloc(void) {
    static const size_t classes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    for (uint32_t i = 0; i < 8; i++) {
        void *p = kmalloc(classes[i]);
        MT_CHECK(p != NULL, "warmup alloc");
        kfree(p);
    }
    uint64_t base = buddy_available_pages();
    mt_kmalloc_sizes(base);
    mt_kmalloc_large(base);
    mt_kmalloc_invalid(base);
    mt_kmalloc_zero(base);
    mt_kmalloc_realloc(base);
    mt_kmalloc_stress(base);
    MT_CHECK((mt_rflags() & RFLAGS_IF) == mt_irq_orig, "irq state after kmalloc");
}

static inline int mem_test_run(void) {
    mt_checks = 0;
    mt_fails = 0;
    mt_irq_orig = mt_rflags() & RFLAGS_IF;
    uint64_t start = buddy_available_pages();
    print("Running memory tests...\n");
    uint64_t mark = mt_fails;
    mt_test_spinlock();
    mt_report("spinlock", mark);
    mark = mt_fails;
    mt_test_buddy();
    mt_report("buddy", mark);
    mark = mt_fails;
    mt_test_slub();
    mt_report("slub", mark);
    mark = mt_fails;
    mt_test_kmalloc();
    mt_report("kmalloc", mark);
    uint64_t end = buddy_available_pages();
    MT_CHECK(end <= start && start - end <= MT_LEAK_SLACK, "total page loss within retained slabs");
    print("Memory tests: ");
    print_uint(mt_checks);
    print(" checks, ");
    print_uint(mt_fails);
    print(" failed\n");
    return mt_fails == 0;
}

#endif
