#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>

int buddy_init(uint64_t bootstrap_bitmap_phys, uint64_t bootstrap_bitmap_bytes);
uint64_t buddy_alloc_order(uint8_t order);
int buddy_free_order(uint64_t phys, uint8_t order);
uint64_t buddy_available_pages(void);

#endif
