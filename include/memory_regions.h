#ifndef MEMORY_REGIONS_H
#define MEMORY_REGIONS_H

#include <stdint.h>

#define MAX_MEM_REGIONS 64

struct mem_region {
    uint64_t base;
    uint64_t end;
};

int memory_regions_init(uint64_t multiboot_data);

uint32_t memory_region_count(void);

const struct mem_region *memory_region_get(uint32_t index);

uint64_t memory_regions_total_bytes(void);

uint64_t memory_regions_highest_end(void);

#endif
