#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>
#include "memory_regions.h"

struct mem_region bitmap_space(uint64_t num_bytes, uint64_t kstart, uint64_t kend, uint64_t info_start, uint64_t info_end);
void bitmap_init(uint64_t base_phys, uint64_t bytes);
void mark_free_memory(uint64_t kstart, uint64_t kend, uint64_t info_start, uint64_t info_end);
uint64_t bitmap_frame_count(void);
int frame_is_free(uint64_t frame);
void mark_frame(uint64_t frame, int value);
uint64_t alloc_frame_basic(void);
uint64_t alloc_frame_in_range(uint64_t first_frame, uint64_t end_frame);
uint64_t count_free_frames(void);

#endif
