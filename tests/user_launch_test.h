#ifndef USER_LAUNCH_TEST_H
#define USER_LAUNCH_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/address_space.h"
#include "../include/buddy.h"
#include "../include/phys.h"
#include "../include/display.h"
#include "../include/thread.h"
#include "../include/sched.h"

static inline void launch_map_page(uint64_t pml4_phys, uint64_t vaddr, uint64_t phys_addr, uint64_t flags) {
    volatile uint64_t *pml4 = (volatile uint64_t *)phys_to_virt(pml4_phys);
    uint32_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint32_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint32_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint32_t pt_idx   = (vaddr >> 12) & 0x1FF;

    // 1. PDPT
    uint64_t pdpt_phys;
    if (!(pml4[pml4_idx] & 1)) {
        pdpt_phys = buddy_alloc_order(0);
        volatile uint64_t *pdpt = (volatile uint64_t *)phys_to_virt(pdpt_phys);
        for (int i = 0; i < 512; i++) pdpt[i] = 0;
        pml4[pml4_idx] = pdpt_phys | 0x3 | 0x4; // Present, Writable, User
    } else {
        pdpt_phys = pml4[pml4_idx] & 0x000FFFFFFFFFF000ULL;
    }
    volatile uint64_t *pdpt = (volatile uint64_t *)phys_to_virt(pdpt_phys);

    // 2. PD
    uint64_t pd_phys;
    if (!(pdpt[pdpt_idx] & 1)) {
        pd_phys = buddy_alloc_order(0);
        volatile uint64_t *pd = (volatile uint64_t *)phys_to_virt(pd_phys);
        for (int i = 0; i < 512; i++) pd[i] = 0;
        pdpt[pdpt_idx] = pd_phys | 0x3 | 0x4;
    } else {
        pd_phys = pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL;
    }
    volatile uint64_t *pd = (volatile uint64_t *)phys_to_virt(pd_phys);

    // 3. PT
    uint64_t pt_phys;
    if (!(pd[pd_idx] & 1)) {
        pt_phys = buddy_alloc_order(0);
        volatile uint64_t *pt = (volatile uint64_t *)phys_to_virt(pt_phys);
        for (int i = 0; i < 512; i++) pt[i] = 0;
        pd[pd_idx] = pt_phys | 0x3 | 0x4;
    } else {
        pt_phys = pd[pd_idx] & 0x000FFFFFFFFFF000ULL;
    }
    volatile uint64_t *pt = (volatile uint64_t *)phys_to_virt(pt_phys);

    // 4. PTE
    pt[pt_idx] = (phys_addr & 0x000FFFFFFFFFF000ULL) | flags;
}

#define LAUNCH_P 0x1ull
#define LAUNCH_W 0x2ull
#define LAUNCH_U 0x4ull
#define LAUNCH_NX (1ull << 63)

static inline void launch_zero_page(uint64_t phys) {
    volatile uint64_t *p = (volatile uint64_t *)phys_to_virt(phys);
    for (uint32_t i = 0; i < 512; i++)
        p[i] = 0;
}

static inline void launch_user_test(void) {
    print("[KERNEL] Preparing first jump to user mode...\n");

    struct address_space *uas = as_create();
    if (!uas) {
        print("[KERNEL] Failed to create user address space\n");
        return;
    }

    uint64_t code_phys = buddy_alloc_order(0);
    uint64_t stack_phys = buddy_alloc_order(0);

    if (!code_phys || !stack_phys) {
        print("[KERNEL] Failed to allocate physical pages for user test\n");
        return;
    }

    launch_zero_page(code_phys);
    launch_zero_page(stack_phys);

    uint64_t user_code_vaddr = 0x400000ULL;
    uint64_t user_stack_top = 0x800000ULL;

    launch_map_page(uas->pml4_phys, user_code_vaddr, code_phys, LAUNCH_P | LAUNCH_U);
    launch_map_page(uas->pml4_phys, user_stack_top - 0x1000, stack_phys, LAUNCH_P | LAUNCH_W | LAUNCH_U | LAUNCH_NX);

    volatile uint8_t *code_ptr = (volatile uint8_t *)phys_to_virt(code_phys);
    code_ptr[0] = 0x8B;
    code_ptr[1] = 0x04;
    code_ptr[2] = 0x25;
    code_ptr[3] = 0x00;
    code_ptr[4] = 0x00;
    code_ptr[5] = 0x00;
    code_ptr[6] = 0x00;
    print("[KERNEL] Switching address space and jumping to user mode via iretq...\n");

    __asm__ volatile ("cli" ::: "memory");
    thread_current()->as = uas;
    as_switch(uas);

    __asm__ volatile (
        "swapgs\n\t"
        "push $0x1B\n\t"
        "push %0\n\t"
        "push $0x202\n\t"
        "push $0x23\n\t"
        "push %1\n\t"
        "iretq\n\t"
        :
        : "r" (user_stack_top), "r" (user_code_vaddr)
        : "memory"
    );
    __builtin_unreachable();
}

static void launch_user_thread(void *arg) {
    (void)arg;
    launch_user_test();
}
#endif
