#include <stdint.h>
#include "../include/paging.h"
#include "../include/memory_map.h"
#include "../include/memory_regions.h"
#include "../include/display.h"
#include "../include/phys.h"

#define PAGE_SIZE 4096ull
#define TWO_MIB 0x200000ull
#define CANONICAL_LIMIT (1ull << 47)
#define ADDRESS_MASK 0x000FFFFFFFFFF000ull
#define FLAG_PRESENT 0x1ull
#define FLAG_HUGE 0x80ull
#define TABLE_FLAGS 0x3ull
#define RAM_FLAGS 0x3ull
#define MMIO_FLAGS 0x1Bull
#define VGA_ADDRESS 0xB8000ull

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

int new_tree(uint64_t kernel_phys_start, uint64_t kernel_phys_end,uint64_t kernel_virt_start, uint64_t kernel_virt_end) {
    uint64_t kernel_phys_size = kernel_phys_end - kernel_phys_start;
    uint64_t kernel_virt_size = kernel_virt_end - kernel_virt_start;
    if (memory_region_count() == 0 || memory_regions_highest_end() >= CANONICAL_LIMIT ||
        kernel_phys_end <= kernel_phys_start ||
        kernel_virt_end <= kernel_virt_start ||
        kernel_phys_size != kernel_virt_size ||
        kernel_virt_start < HHDM_BASE ||
        (kernel_phys_start & (PAGE_SIZE - 1)) != 0 ||
        (kernel_virt_start & (PAGE_SIZE - 1)) != 0) {
        print("Invalid page-table range\n");
        return 0;
    }
    uint64_t pml4_phys = alloc_frame_basic();
    if (pml4_phys == 0) {
        print("Could not allocate PML4\n");
        return 0;
    }
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys);
    if (!map_range(pml4, kernel_virt_start, kernel_phys_start,
                   kernel_virt_size, RAM_FLAGS)) {
        print("Could not map the higher-half kernel\n");
        return 0;
    }
    for (uint32_t r = 0; r < memory_region_count(); r++) {
        const struct mem_region *region = memory_region_get(r);
        if (!map_range(pml4, HHDM_BASE + region->base, region->base,region->end - region->base, RAM_FLAGS)) {
            print("Could not build the direct map\n");
            return 0;
        }
    }
    if (!map_4k(pml4, HHDM_BASE + VGA_ADDRESS,
                VGA_ADDRESS, MMIO_FLAGS)) {
        print("Could not map VGA in the direct map\n");
        return 0;
    }
    write_cr3(pml4_phys);
    phys_set_hhdm_offset(HHDM_BASE);
    return 1;
}
