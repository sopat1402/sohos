#include <stdint.h>
#include "../include/paging.h"
#include "../include/memory_map.h"
#include "../include/memory_regions.h"
#include "../include/display.h"
#include "../include/phys.h"
#include "../include/tlb.h"

#define PAGE_SIZE 4096ull
#define TWO_MIB 0x200000ull
#define CANONICAL_LIMIT (1ull << 47)
#define ADDRESS_MASK 0x000FFFFFFFFFF000ull
#define TABLE_FLAGS 0x3ull
#define RAM_FLAGS 0x3ull
#define VGA_ADDRESS 0xB8000ull
#define FLAG_PRESENT 0x1ull
#define FLAG_WRITABLE 0x2ull
#define FLAG_USER 0x4ull
#define FLAG_HUGE 0x80ull
#define FLAG_NX (1ULL << 63)
#define MMIO_FLAGS (0x1Bull | FLAG_NX)

extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __bss_end[];

static inline void write_cr3(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

static uint64_t *next_table(uint64_t *table, uint64_t index){
    if ((table[index] & FLAG_PRESENT) == 0){
        uint64_t frame = alloc_frame_basic();
        if (frame == 0)
            return 0;
        table[index] = frame | TABLE_FLAGS;
    }
    return (uint64_t *)phys_to_virt(table[index] & ADDRESS_MASK);
}

static int map_4k(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags){
    uint64_t *pdpt = next_table(pml4, (virt >> 39) & 0x1FF);
    if (pdpt == 0)
        return 0;
    uint64_t *pd = next_table(pdpt, (virt >> 30) & 0x1FF);
    if (pd == 0)
        return 0;
    uint64_t pd_index = (virt >> 21) & 0x1FF;
    if (pd[pd_index] & FLAG_HUGE)
        return 0;
    uint64_t *pt = next_table(pd, pd_index);
    if (pt == 0)
        return 0;
    pt[(virt >> 12) & 0x1FF] = phys | flags;
    return 1;
}

static int map_2m(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags){
    uint64_t *pdpt = next_table(pml4, (virt >> 39) & 0x1FF);
    if (pdpt == 0)
        return 0;
    uint64_t *pd = next_table(pdpt, (virt >> 30) & 0x1FF);
    if (pd == 0)
        return 0;
    uint64_t pd_index = (virt >> 21) & 0x1FF;
    if (pd[pd_index] & FLAG_PRESENT)
        return 0;
    pd[pd_index] = phys | flags | FLAG_HUGE;
    return 1;
}

static int map_range(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags){
    uint64_t end = virt + size;
    while (virt < end){
        if (((virt | phys) & (TWO_MIB - 1)) == 0 && end - virt >= TWO_MIB){
            if (!map_2m(pml4, virt, phys, flags))
                return 0;
            virt += TWO_MIB;
            phys += TWO_MIB;
        } else {
            if (!map_4k(pml4, virt, phys, flags))
                return 0;
            virt += PAGE_SIZE;
            phys += PAGE_SIZE;
        }
    }
    return 1;
}


int new_tree(uint64_t kernel_phys_start, uint64_t kernel_phys_end, uint64_t kernel_virt_start, uint64_t kernel_virt_end, uint64_t trampoline_phys, uint64_t lapic_phys){
    uint64_t kernel_phys_size = kernel_phys_end - kernel_phys_start;
    uint64_t kernel_virt_size = kernel_virt_end - kernel_virt_start;
    if (memory_region_count() == 0 ||
        memory_regions_highest_end() >= CANONICAL_LIMIT ||
        kernel_phys_end <= kernel_phys_start ||
        kernel_virt_end <= kernel_virt_start ||
        kernel_phys_size != kernel_virt_size ||
        kernel_virt_start < HHDM_BASE ||
        (kernel_phys_start & (PAGE_SIZE - 1)) != 0 ||
        (kernel_virt_start & (PAGE_SIZE - 1)) != 0 ||
        (trampoline_phys != 0 &&
         ((trampoline_phys & (PAGE_SIZE - 1)) != 0 ||
          trampoline_phys >= 0x100000ull)) ||
        (lapic_phys != 0 &&
         ((lapic_phys & (PAGE_SIZE - 1)) != 0 ||
          lapic_phys >= CANONICAL_LIMIT))) {
        print("Invalid page-table range\n");
        return 0;
    }
    uint64_t pml4_phys = alloc_frame_basic();
    if (pml4_phys == 0) {
        print("Could not allocate PML4\n");
        return 0;
    }
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys);

    // .text read only + executable nx=0, writable=0
    uint64_t text_size = (uint64_t)((uintptr_t)__text_end - (uintptr_t)__text_start);
    uint64_t text_phys = kernel_phys_start + ((uintptr_t)__text_start - kernel_virt_start);
    if (!map_range(pml4, (uintptr_t)__text_start, text_phys, text_size, FLAG_PRESENT)) {
        print("Could not map kernel .text\n");
        return 0;
    }

    // .rodata read only + not executable writable=0, nx=1
    uint64_t rodata_size = (uint64_t)((uintptr_t)__rodata_end - (uintptr_t)__rodata_start);
    uint64_t rodata_phys = kernel_phys_start + ((uintptr_t)__rodata_start - kernel_virt_start);
    if (!map_range(pml4, (uintptr_t)__rodata_start, rodata_phys, rodata_size, FLAG_PRESENT | FLAG_NX)) {
        print("Could not map kernel .rodata\n");
        return 0;
    }

    // .data and .bss read + write + not executable writable=1, nx=1
    uint64_t data_bss_size = (uint64_t)((uintptr_t)__bss_end - (uintptr_t)__data_start);
    uint64_t data_bss_phys = kernel_phys_start + ((uintptr_t)__data_start - kernel_virt_start);
    if (!map_range(pml4, (uintptr_t)__data_start, data_bss_phys, data_bss_size, FLAG_PRESENT | FLAG_WRITABLE | FLAG_NX)) {
        print("Could not map kernel .data/.bss\n");
        return 0;
    }

    #define TRAMPOLINE_INIT_FLAGS (FLAG_PRESENT | FLAG_WRITABLE | FLAG_NX)
    if (trampoline_phys != 0 && !map_4k(pml4, trampoline_phys, trampoline_phys, TRAMPOLINE_INIT_FLAGS)) {
        print("Could not identity-map the AP trampoline\n");
        return 0;
    }

    #define DIRECT_RAM_FLAGS (FLAG_PRESENT | FLAG_WRITABLE | FLAG_NX)

    uint64_t text_phys_start = kernel_phys_start + ((uintptr_t)__text_start - kernel_virt_start);
    uint64_t text_phys_end   = kernel_phys_start + ((uintptr_t)__text_end - kernel_virt_start);
    uint64_t rodata_phys_start = kernel_phys_start + ((uintptr_t)__rodata_start - kernel_virt_start);
    uint64_t rodata_phys_end   = kernel_phys_start + ((uintptr_t)__rodata_end - kernel_virt_start);
    uint64_t data_phys_start = kernel_phys_start + ((uintptr_t)__data_start - kernel_virt_start);
    uint64_t bss_phys_end    = kernel_phys_start + ((uintptr_t)__bss_end - kernel_virt_start);

    for (uint32_t r = 0; r < memory_region_count(); r++) {
        const struct mem_region *region = memory_region_get(r);
        uint64_t reg_base = region->base;
        uint64_t reg_end = region->end;

        if (reg_end <= kernel_phys_start || reg_base >= kernel_phys_end) {
            if (!map_range(pml4, HHDM_BASE + reg_base, reg_base, reg_end - reg_base, DIRECT_RAM_FLAGS)) {
                print("Could not build the direct map\n");
                return 0;
            }
        } else {
            if (reg_base < kernel_phys_start) {
                if (!map_range(pml4, HHDM_BASE + reg_base, reg_base, kernel_phys_start - reg_base, DIRECT_RAM_FLAGS)) {
                    print("Could not build direct map (pre-kernel)\n");
                    return 0;
                }
            }

            if (kernel_phys_start < text_phys_start) {
                if (!map_range(pml4, HHDM_BASE + kernel_phys_start, kernel_phys_start, text_phys_start - kernel_phys_start, DIRECT_RAM_FLAGS))
                    return 0;
            }

            if (!map_range(pml4, HHDM_BASE + text_phys_start, text_phys_start, text_phys_end - text_phys_start, FLAG_PRESENT | FLAG_NX))
                return 0;

            if (!map_range(pml4, HHDM_BASE + rodata_phys_start, rodata_phys_start, rodata_phys_end - rodata_phys_start, FLAG_PRESENT | FLAG_NX))
                return 0;

            if (!map_range(pml4, HHDM_BASE + data_phys_start, data_phys_start, bss_phys_end - data_phys_start, FLAG_PRESENT | FLAG_WRITABLE | FLAG_NX))
                return 0;

            if (bss_phys_end < kernel_phys_end) {
                if (!map_range(pml4, HHDM_BASE + bss_phys_end, bss_phys_end, kernel_phys_end - bss_phys_end, DIRECT_RAM_FLAGS))
                    return 0;
            }

            if (reg_end > kernel_phys_end) {
                if (!map_range(pml4, HHDM_BASE + kernel_phys_end, kernel_phys_end, reg_end - kernel_phys_end, DIRECT_RAM_FLAGS)) {
                    print("Could not build direct map (post-kernel)\n");
                    return 0;
                }
            }
        }
    }

    if (lapic_phys != 0 && !map_4k(pml4, HHDM_BASE + lapic_phys, lapic_phys, MMIO_FLAGS)) {
        print("Could not map the local APIC\n");
        return 0;
    }
    if (!map_4k(pml4, HHDM_BASE + VGA_ADDRESS, VGA_ADDRESS, MMIO_FLAGS)) {
        print("Could not map VGA in the direct map\n");
        return 0;
    }
    write_cr3(pml4_phys);
    phys_set_hhdm_offset(HHDM_BASE);
    return 1;
}

int vm_set_flags_local(uint64_t pml4_phys, uint64_t virt_addr, uint64_t set_bits, uint64_t clear_bits) {
    if (((set_bits | clear_bits) & ADDRESS_MASK) != 0 || (set_bits & clear_bits) != 0)
        return 0;

    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys & ADDRESS_MASK);

    uint64_t e4_idx = (virt_addr >> 39) & 0x1FF;
    if ((pml4[e4_idx] & FLAG_PRESENT) == 0) return 0;
    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[e4_idx] & ADDRESS_MASK);

    uint64_t e3_idx = (virt_addr >> 30) & 0x1FF;
    if ((pdpt[e3_idx] & FLAG_PRESENT) == 0) return 0;
    if (pdpt[e3_idx] & FLAG_HUGE) return 0;
    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[e3_idx] & ADDRESS_MASK);

    uint64_t e2_idx = (virt_addr >> 21) & 0x1FF;
    if ((pd[e2_idx] & FLAG_PRESENT) == 0) return 0;
    if (pd[e2_idx] & FLAG_HUGE) return 0;
    uint64_t *pt = (uint64_t *)phys_to_virt(pd[e2_idx] & ADDRESS_MASK);

    uint64_t e1_idx = (virt_addr >> 12) & 0x1FF;
    uint64_t old = __atomic_load_n(&pt[e1_idx], __ATOMIC_RELAXED);
    if ((old & FLAG_PRESENT) == 0) return 0;

    uint64_t entry = (old | set_bits) & ~clear_bits;
    __atomic_store_n(&pt[e1_idx], entry, __ATOMIC_RELEASE);

    __asm__ volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
    return 1;
}

int vm_set_flags(uint64_t pml4_phys, uint64_t virt_addr, uint64_t set_bits, uint64_t clear_bits) {
    if (!vm_set_flags_local(pml4_phys, virt_addr, set_bits, clear_bits))
        return 0;
    tlb_shootdown();
    return 1;
}
