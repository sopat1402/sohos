#ifndef ISOLATION_TEST_H
#define ISOLATION_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/address_space.h"
#include "../include/buddy.h"
#include "../include/phys.h"
#include "../include/kmalloc.h"
#include "../include/display.h"

// Self-contained page table mapper for the test
static inline void test_map_page(uint64_t pml4_phys, uint64_t vaddr, uint64_t phys_addr, uint64_t flags) {
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

static inline void run_isolation_test(void) {
    print("\n--- Starting Address Space Isolation Test ---\n");

    // 1. Create two independent address spaces
    struct address_space *as1 = as_create();
    struct address_space *as2 = as_create();

    if (!as1 || !as2) {
        print("FAIL: Failed to create address spaces for isolation test.\n");
        return;
    }

    uint64_t test_vaddr = 0x400000ULL; // 4MB mark

    // 2. Allocate two distinct physical pages
    uint64_t phys1 = buddy_alloc_order(0);
    uint64_t phys2 = buddy_alloc_order(0);

    if (!phys1 || !phys2) {
        print("FAIL: Failed to allocate physical pages for isolation test.\n");
        if (as1) as_put(as1);
        if (as2) as_put(as2);
        return;
    }

    // Write distinct test values into the physical pages via HHDM
    volatile uint64_t *val1_ptr = (volatile uint64_t *)phys_to_virt(phys1);
    volatile uint64_t *val2_ptr = (volatile uint64_t *)phys_to_virt(phys2);
    *val1_ptr = 0x1111111122222222ULL;
    *val2_ptr = 0x9999999988888888ULL;

    // 3. Map the same virtual address to different physical pages in each AS
    // Flags: Present (1) | Writable (2) | User (4) = 0x7
    test_map_page(as1->pml4_phys, test_vaddr, phys1, 0x7);
    test_map_page(as2->pml4_phys, test_vaddr, phys2, 0x7);

    // 4. Verify isolation by switching CR3 and reading the shared virtual address
    uint64_t old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));

    // Switch to AS1
    __asm__ volatile("mov %0, %%cr3" : : "r"(as1->pml4_phys) : "memory");
    volatile uint64_t *shared_target = (volatile uint64_t *)test_vaddr;
    uint64_t read_val1 = *shared_target;

    // Switch to AS2
    __asm__ volatile("mov %0, %%cr3" : : "r"(as2->pml4_phys) : "memory");
    uint64_t read_val2 = *shared_target;

    // Restore original CR3
    __asm__ volatile("mov %0, %%cr3" : : "r"(old_cr3) : "memory");

    print("AS1 read value: 0x");
    print_hex(read_val1);
    print("\n");

    print("AS2 read value: 0x");
    print_hex(read_val2);
    print("\n");

    if (read_val1 == 0x1111111122222222ULL && read_val2 == 0x9999999988888888ULL) {
        print("PASS: Data isolation verified successfully between identical virtual addresses!\n");
    } else {
        print("FAIL: Data isolation mismatch!\n");
    }

    // 5. Cleanup address spaces (triggers as_destroy and frees page tables)
    as_put(as1);
    as_put(as2);

    // Free the test physical data pages
    buddy_free_order(phys1, 0);
    buddy_free_order(phys2, 0);

    print("--- Isolation Test Complete ---\n");
}

#endif
