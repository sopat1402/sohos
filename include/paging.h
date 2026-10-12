#ifndef PAGING
#define PAGING 1

#include <stdint.h>

int new_tree(uint64_t kernel_phys_start,
             uint64_t kernel_phys_end,
             uint64_t kernel_virt_start,
             uint64_t kernel_virt_end,
             uint64_t trampoline_phys,
             uint64_t lapic_phys);

#endif
