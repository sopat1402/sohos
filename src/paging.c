#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/display.h"
#include "../include/phys.h"

#define PAGE_SIZE 4096ull
#define TABLE_ENTRIES 512ull
#define TWO_MIB (TABLE_ENTRIES * PAGE_SIZE)
#define ONE_GIB (TABLE_ENTRIES * TWO_MIB)
#define ONE_PDPT_SPAN (TABLE_ENTRIES * ONE_GIB)
#define LOW_CANONICAL_LIMIT (1ull << 47)
#define BOOTSTRAP_MAP_END ONE_GIB
#define PAGE_FLAGS 0x3ull
#define LARGE_PAGE_FLAGS 0x83ull
#define VGA_ADDRESS 0xB8000ull
#define VGA_FLAGS 0x1Bull

static inline void write_cr3(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

int new_tree(uint64_t highest_usable_end) {
    uint64_t bitmap_bits = bitmap_frame_count();

    if (bitmap_bits == 0 || highest_usable_end == 0 || highest_usable_end > LOW_CANONICAL_LIMIT) {
        print("Invalid page-table range\n");
        return 0;
    }

    uint64_t max_frames = highest_usable_end / PAGE_SIZE;
    if (highest_usable_end % PAGE_SIZE != 0)
        max_frames++;

    if (max_frames > bitmap_bits) {
        print("Bitmap does not cover the usable address range\n");
        return 0;
    }

    uint64_t pml4_phys = alloc_frame_basic();
    if (pml4_phys == 0) {
        print("Could not allocate PML4\n");
        return 0;
    }

    uint64_t *pml4 = phys_to_virt(pml4_phys);
    uint64_t pml4_count = highest_usable_end / ONE_PDPT_SPAN;
    if (highest_usable_end % ONE_PDPT_SPAN != 0)
        pml4_count++;

    for (uint64_t pml4_index = 0; pml4_index < pml4_count; pml4_index++) {
        uint64_t pdpt_phys = alloc_frame_basic();
        if (pdpt_phys == 0) {
            print("Could not allocate PDPT\n");
            return 0;
        }

        uint64_t *pdpt = phys_to_virt(pdpt_phys);
        pml4[pml4_index] = pdpt_phys | PAGE_FLAGS;

        uint64_t pml4_base = pml4_index * ONE_PDPT_SPAN;
        uint64_t pml4_end = pml4_base + ONE_PDPT_SPAN;
        if (pml4_end > highest_usable_end)
            pml4_end = highest_usable_end;

        uint64_t pdpt_count = (pml4_end - pml4_base) / ONE_GIB;
        if ((pml4_end - pml4_base) % ONE_GIB != 0)
            pdpt_count++;

        for (uint64_t pdpt_index = 0; pdpt_index < pdpt_count; pdpt_index++) {
            uint64_t pd_phys = alloc_frame_basic();
            if (pd_phys == 0) {
                print("Could not allocate PD\n");
                return 0;
            }

            uint64_t *pd = phys_to_virt(pd_phys);
            pdpt[pdpt_index] = pd_phys | PAGE_FLAGS;

            uint64_t pd_base = pml4_base + pdpt_index * ONE_GIB;
            uint64_t pd_end = pd_base + ONE_GIB;
            if (pd_end > highest_usable_end)
                pd_end = highest_usable_end;

            for (uint64_t pd_index = 0; pd_index < TABLE_ENTRIES; pd_index++) {
                uint64_t region_base = pd_base + pd_index * TWO_MIB;
                if (region_base >= pd_end)
                    break;

                uint64_t region_end = region_base + TWO_MIB;
                if (region_end > pd_end)
                    region_end = pd_end;

                if (region_base < BOOTSTRAP_MAP_END) {
                    if (region_base == 0 || region_end < region_base + TWO_MIB) {
                        uint64_t pt_phys = alloc_frame_basic();
                        if (pt_phys == 0) {
                            print("Could not allocate low-memory PT\n");
                            return 0;
                        }

                        uint64_t *pt = phys_to_virt(pt_phys);
                        pd[pd_index] = pt_phys | PAGE_FLAGS;

                        for (uint64_t pt_index = 0; pt_index < TABLE_ENTRIES; pt_index++) {
                            uint64_t physical_address = region_base + pt_index * PAGE_SIZE;

                            if (physical_address >= region_end || physical_address == 0) {
                                pt[pt_index] = 0;
                            } else if (physical_address == VGA_ADDRESS) {
                                pt[pt_index] = physical_address | VGA_FLAGS;
                            } else {
                                pt[pt_index] = physical_address | PAGE_FLAGS;
                            }
                        }
                    } else {
                        pd[pd_index] = region_base | LARGE_PAGE_FLAGS;
                    }

                    continue;
                }

                uint64_t first_frame = region_base / PAGE_SIZE;
                uint64_t last_frame = region_end / PAGE_SIZE;
                if (region_end % PAGE_SIZE != 0)
                    last_frame++;

                uint64_t frames_in_region = last_frame - first_frame;
                int any_free = 0;
                int all_free = frames_in_region == TABLE_ENTRIES;

                for (uint64_t frame = first_frame; frame < last_frame; frame++) {
                    if (frame >= bitmap_bits) {
                        all_free = 0;
                        continue;
                    }

                    if (frame_is_free(frame))
                        any_free = 1;
                    else
                        all_free = 0;
                }

                if (all_free && region_end == region_base + TWO_MIB) {
                    pd[pd_index] = region_base | LARGE_PAGE_FLAGS;
                    continue;
                }

                if (!any_free)
                    continue;

                uint64_t pt_phys = alloc_frame_basic();
                if (pt_phys == 0) {
                    print("Could not allocate high-memory PT\n");
                    return 0;
                }

                uint64_t *pt = phys_to_virt(pt_phys);
                pd[pd_index] = pt_phys | PAGE_FLAGS;

                for (uint64_t pt_index = 0; pt_index < TABLE_ENTRIES; pt_index++) {
                    uint64_t frame = first_frame + pt_index;
                    uint64_t physical_address = frame * PAGE_SIZE;

                    if (frame >= last_frame || frame >= bitmap_bits) {
                        pt[pt_index] = 0;
                        continue;
                    }

                    if (!frame_is_free(frame))
                        pt[pt_index] = 0;
                    else
                        pt[pt_index] = physical_address | PAGE_FLAGS;
                }
            }
        }
    }

    write_cr3(pml4_phys);
    return 1;
}

