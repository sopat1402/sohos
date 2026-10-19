#include <stdint.h>
#include <stddef.h>
#include "../include/address_space.h"
#include "../include/buddy.h"
#include "../include/phys.h"
#include "../include/kmalloc.h"

#define AS_SLOTS 512u
#define AS_KERNEL_FIRST_SLOT 256u
#define AS_ADDR_MASK 0x000FFFFFFFFFF000ull

#define FLAG_PRESENT 0x1ull
#define FLAG_WRITABLE 0x2ull
#define FLAG_USER 0x4ull
#define FLAG_HUGE 0x80ull
#define FLAG_NX (1ULL << 63)

struct address_space kernel_as;

static uint64_t as_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & AS_ADDR_MASK;
}

static void as_zero_page(void *page) {
    volatile uint64_t *p = (volatile uint64_t *)page;
    for (uint32_t i = 0; i < 512; i++)
        p[i] = 0;
}

int kernel_as_init(void) {
    uint64_t cr3 = as_read_cr3();
    volatile uint64_t *pml4 = (volatile uint64_t *)phys_to_virt(cr3);

    for (uint32_t i = AS_KERNEL_FIRST_SLOT; i < AS_SLOTS; i++) {
        if (pml4[i] & FLAG_PRESENT)
            continue;

        uint64_t table = buddy_alloc_order(0);
        if (table == 0)
            return 0;

        as_zero_page(phys_to_virt(table));
        pml4[i] = table | FLAG_PRESENT | FLAG_WRITABLE;
    }

    kernel_as.pml4_phys = cr3;
    kernel_as.refcount = 1;
    kernel_as.cpu_mask = 0;
    return 1;
}

struct address_space *as_create(void) {
    if (kernel_as.pml4_phys == 0)
        return NULL;

    struct address_space *as = (struct address_space *)kzalloc(sizeof(*as));
    if (as == NULL)
        return NULL;

    uint64_t pml4_phys = buddy_alloc_order(0);
    if (pml4_phys == 0) {
        kfree(as);
        return NULL;
    }

    volatile uint64_t *dst = (volatile uint64_t *)phys_to_virt(pml4_phys);
    volatile uint64_t *src = (volatile uint64_t *)phys_to_virt(kernel_as.pml4_phys);

    for (uint32_t i = 0; i < AS_KERNEL_FIRST_SLOT; i++)
        dst[i] = 0;
    for (uint32_t i = AS_KERNEL_FIRST_SLOT; i < AS_SLOTS; i++)
        dst[i] = src[i];

    as->pml4_phys = pml4_phys;
    as->refcount = 1;
    as->cpu_mask = 0;
    return as;
}

static int as_free_level(uint64_t table_phys, uint32_t level) {
    volatile uint64_t *t = (volatile uint64_t *)phys_to_virt(table_phys);
    int ok = 1;

    for (uint32_t i = 0; i < 512; i++) {
        uint64_t e = t[i];
        if ((e & FLAG_PRESENT) == 0)
            continue;

        uint64_t phys = e & AS_ADDR_MASK;

        if (level == 1) {
            if (buddy_free_order(phys, 0) != 1)
                ok = 0;
            continue;
        }

        if (level <= 3 && (e & FLAG_HUGE) != 0) {
            if (buddy_free_order(phys, level == 2 ? 9 : 18) != 1)
                ok = 0;
            continue;
        }

        if (!as_free_level(phys, level - 1))
            ok = 0;
    }

    if (buddy_free_order(table_phys, 0) != 1)
        ok = 0;
    return ok;
}

int as_destroy(struct address_space *as) {
    if (as == NULL || as == &kernel_as)
        return 0;
    if (__atomic_load_n(&as->cpu_mask, __ATOMIC_ACQUIRE) != 0)
        return 0;

    volatile uint64_t *pml4 = (volatile uint64_t *)phys_to_virt(as->pml4_phys);
    int ok = 1;

    for (uint32_t i = 0; i < AS_KERNEL_FIRST_SLOT; i++) {
        uint64_t e = pml4[i];
        if ((e & FLAG_PRESENT) == 0)
            continue;
        if (!as_free_level(e & AS_ADDR_MASK, 3))
            ok = 0;
        pml4[i] = 0;
    }

    if (buddy_free_order(as->pml4_phys, 0) != 1)
        ok = 0;

    kfree(as);
    return ok;
}

struct address_space *as_get(struct address_space *as) {
    __atomic_fetch_add(&as->refcount, 1, __ATOMIC_ACQ_REL);
    return as;
}

void as_put(struct address_space *as) {
    if (as == NULL || as == &kernel_as)
        return;
    if (__atomic_sub_fetch(&as->refcount, 1, __ATOMIC_ACQ_REL) == 0)
        as_destroy(as);
}
