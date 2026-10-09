#ifndef PAGING
#define PAGING 1

#include <stdint.h>

int new_tree(uint8_t *bitmap_start, uint8_t *bitmap_end, uint64_t highest_usable_end);

#endif
