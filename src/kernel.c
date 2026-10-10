#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/memory_regions.h"
#include "../include/display.h"
#include "../include/paging.h"
#include "../include/idt.h"
#include "../include/phys.h"

#define PAGE_SIZE 4096ull
#define BOOTSTRAP_LIMIT 0x40000000ull

extern char kernel_start;
extern char kernel_end;

void kmain(uint32_t magic, uint64_t multiboot_data) {
    idt_init();

    if (magic != 0x36d76289) {
        print("Invalid Multiboot magic\n");
        return;
    }

    if (multiboot_data == 0 || multiboot_data >= BOOTSTRAP_LIMIT || BOOTSTRAP_LIMIT - multiboot_data < sizeof(uint32_t)) {
        print("Multiboot info is outside the bootstrap map\n");
        return;
    }

    uint32_t multiboot_info_size = *(const uint32_t *)phys_to_virt(multiboot_data);
    if (multiboot_info_size < 16 || multiboot_info_size > BOOTSTRAP_LIMIT - multiboot_data) {
        print("Invalid Multiboot info size\n");
        return;
    }

    uint64_t kstart = kernel_virt_to_phys(&kernel_start);
    uint64_t kend = kernel_virt_to_phys(&kernel_end);

    if (kstart >= kend || kend > BOOTSTRAP_LIMIT) {
        print("Kernel is outside the bootstrap map\n");
        return;
    }

    if (!memory_regions_init(multiboot_data)) {
        print("No available memory reported\n");
        return;
    }

    uint64_t total_usable_bytes = memory_regions_total_bytes();
    uint64_t highest_usable_end = memory_regions_highest_end();

    print("Total usable RAM: ");
    print_size(total_usable_bytes);
    print(" (");
    print_uint(total_usable_bytes);
    print(" bytes)\nHighest usable physical address: 0x");
    print_hex(highest_usable_end);
    print("\n");

    uint64_t max_frames = highest_usable_end / PAGE_SIZE;
    if (highest_usable_end % PAGE_SIZE != 0)
        max_frames++;

    uint64_t bitmap_bytes = max_frames / 8;
    if (max_frames % 8 != 0)
        bitmap_bytes++;

    if (bitmap_bytes == 0) {
        print("Bitmap size is zero\n");
        return;
    }

    uint64_t info_start = multiboot_data;
    uint64_t info_end = multiboot_data + multiboot_info_size;

    struct mem_region bitmap_region = bitmap_space(bitmap_bytes, kstart, kend, info_start, info_end);

    if (bitmap_region.end <= bitmap_region.base || bitmap_region.end > BOOTSTRAP_LIMIT) {
        print("Bitmap is outside the bootstrap map or invalid\n");
        return;
    }

    bitmap_init(bitmap_region.base, bitmap_region.end - bitmap_region.base);
    mark_free_memory(kstart, kend, info_start, info_end);
    print("Free frames before page tables: ");
    print_uint(count_free_frames());
    print("\n");

    if (!new_tree(kstart, kend))
        return;

    print("Direct map live at 0x");
    print_hex(hhdm_offset);
    print("\n");

    uint64_t first_high_frame = BOOTSTRAP_LIMIT / PAGE_SIZE;
    uint64_t high_frame = alloc_frame_in_range(first_high_frame, max_frames);

    if (high_frame == 0) {
        print("No free frame above the bootstrap range\n");
    } else {
        print("Allocated and zeroed high frame at: 0x");
        print_hex(high_frame);
        print("\n");
    }

    print("Free frames after CR3 switch: ");
    print_uint(count_free_frames());
    print("\n");
}
