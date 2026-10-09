#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/display.h"
#include "../include/paging.h"

#define PAGE_SIZE 4096ull
#define BOOTSTRAP_LIMIT 0x40000000ull

extern char kernel_start;
extern char kernel_end;

void kmain(uint32_t magic, uint64_t multiboot_data) {
    if (magic != 0x36d76289) {
        print("Invalid Multiboot magic\n");
        return;
    }

    if (multiboot_data == 0 || multiboot_data >= BOOTSTRAP_LIMIT || BOOTSTRAP_LIMIT - multiboot_data < sizeof(uint32_t)) {
        print("Multiboot info is outside the bootstrap map\n");
        return;
    }

    uint32_t multiboot_info_size = *(const uint32_t *)(uintptr_t)multiboot_data;
    if (multiboot_info_size < 16 || multiboot_info_size > BOOTSTRAP_LIMIT - multiboot_data) {
        print("Invalid Multiboot info size\n");
        return;
    }

    uint64_t kstart = (uint64_t)(uintptr_t)&kernel_start;
    uint64_t kend = (uint64_t)(uintptr_t)&kernel_end;

    if (kstart >= kend || kend > BOOTSTRAP_LIMIT) {
        print("Kernel is outside the bootstrap map\n");
        return;
    }

    uint64_t total_usable_bytes = 0;
    uint64_t highest_usable_end = 0;
    memory_map_stats(multiboot_data, &total_usable_bytes, &highest_usable_end);

    if (total_usable_bytes == 0 || highest_usable_end == 0) {
        print("No available memory reported\n");
        return;
    }

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

    struct multiboot_mmap_entry bitmap_region = bitmap_space(bitmap_bytes, multiboot_data, (uintptr_t)kstart, (uintptr_t)kend);

    if (bitmap_region.length == 0 || bitmap_region.base_addr == 0 || bitmap_region.base_addr >= BOOTSTRAP_LIMIT || bitmap_region.length > BOOTSTRAP_LIMIT - bitmap_region.base_addr || bitmap_region.base_addr % PAGE_SIZE != 0 || bitmap_region.length % PAGE_SIZE != 0) {
        print("Bitmap is outside the bootstrap map or invalid\n");
        return;
    }

    uint64_t bitmap_end_address = bitmap_region.base_addr + bitmap_region.length;
    uint8_t *bitmap_start = (uint8_t *)(uintptr_t)bitmap_region.base_addr;
    uint8_t *bitmap_end = (uint8_t *)(uintptr_t)bitmap_end_address;

    for (uint64_t address = bitmap_region.base_addr; address < bitmap_end_address; address++)
        *(uint8_t *)(uintptr_t)address = 0xFF;

    mark_free_memory(multiboot_data, (uintptr_t)kstart, (uintptr_t)kend, (uintptr_t)bitmap_region.base_addr, (uintptr_t)bitmap_end_address);

    print("Kernel start: ");
    print_hex(kstart);
    print("\nKernel end: ");
    print_hex(kend);
    print("\nBitmap start: ");
    print_hex(bitmap_region.base_addr);
    print("\nBitmap end: ");
    print_hex(bitmap_end_address);
    print("\nFree frames before page tables: ");
    print_uint(count_free_frames(bitmap_start, bitmap_end));
    print("\n");

    if (!new_tree(bitmap_start, bitmap_end, highest_usable_end))
        return;

    uint64_t first_high_frame = BOOTSTRAP_LIMIT / PAGE_SIZE;
    uint64_t high_frame = alloc_frame_in_range(bitmap_start, bitmap_end, first_high_frame, max_frames);

    if (high_frame == 0) {
        print("No free frame above the bootstrap range\n");
    } else {
        print("Allocated and zeroed high frame at: 0x");
        print_hex(high_frame);
        print("\n");
    }

    print("CR3 switched to mixed 4 KiB / 2 MiB identity map\n");
    print("Free frames after CR3 switch: ");
    print_uint(count_free_frames(bitmap_start, bitmap_end));
    print("\n");
}
