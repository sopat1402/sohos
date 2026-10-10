#ifndef PAGING
#define PAGING 1

#include <stdint.h>

int new_tree(uint64_t kernel_phys_start, uint64_t kernel_phys_end,uint64_t kernel_virt_start, uint64_t kernel_virt_end);
#endif
