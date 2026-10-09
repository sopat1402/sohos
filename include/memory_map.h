#ifndef MEMORY
#define MEMORY 1

#include <stdint.h>

struct multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct multiboot_mmap_tag {
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
};

struct multiboot_mmap_entry {
	uint64_t base_addr;
	uint64_t length;
	uint32_t type;
	uint32_t reserved;
};

struct multiboot_mmap_entry bitmap_space(uint64_t num_bytes,uint64_t multiboot_data,uintptr_t kstart,uintptr_t kend);

void mark_free_memory(uint64_t multiboot_data, uintptr_t kstart, uintptr_t kend, uintptr_t bitmap_start, uintptr_t bitmap_end);

void mark_frame(uint8_t *bitmap_start,uint64_t frame,int value);

uint64_t count_free_frames(uint8_t *bitmap_start,uint8_t *bitmap_end);

uint64_t alloc_frame_basic(uint8_t *bitmap_start, uint8_t *bitmap_end);

void memory_map_stats(uint64_t multiboot_data, uint64_t *total_usable_bytes, uint64_t *highest_usable_end);

uint64_t alloc_frame_in_range(uint8_t *bitmap_start, uint8_t *bitmap_end, uint64_t first_frame, uint64_t end_frame);

#endif
