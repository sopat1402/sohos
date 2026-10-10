#ifndef MEMORY
#define MEMORY 1

#include <stdint.h>

struct mem_region bitmap_space(uint64_t num_bytes, uint64_t kstart, uint64_t kend, uint64_t info_start, uint64_t info_end);

void mark_free_memory(uint64_t kstart, uint64_t kend, uint64_t bitmap_start, uint64_t bitmap_end, uint64_t info_start, uint64_t info_end);

void mark_frame(uint8_t *bitmap_start,uint64_t frame,int value);

uint64_t count_free_frames(uint8_t *bitmap_start,uint8_t *bitmap_end);

uint64_t alloc_frame_basic(uint8_t *bitmap_start, uint8_t *bitmap_end);

uint64_t alloc_frame_in_range(uint8_t *bitmap_start, uint8_t *bitmap_end, uint64_t first_frame, uint64_t end_frame);

#endif
