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

uint64_t max_available_memory_address(uint64_t multiboot_data);

struct multiboot_mmap_entry bitmap_space(uint64_t num_bytes,uint64_t multiboot_data,uintptr_t kstart,uintptr_t kend);

void mark_free_memory(uint64_t multiboot_data, uintptr_t kstart, uintptr_t kend, uintptr_t bitmap_start, uintptr_t bitmap_end);

#endif
