#ifndef VMALLOC_H
#define VMALLOC_H

#include <stdint.h>

void vmalloc_init(void);
uint64_t size_to_pages(uint64_t size);
uint64_t vmalloc(uint64_t size);
int vfree(uintptr_t base);

#endif
