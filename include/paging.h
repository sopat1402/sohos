#ifndef PAGING
#define PAGING 1

#include <stdint.h>

int new_tree(uint64_t kernel_phys_start,
             uint64_t kernel_phys_end,
             uint64_t kernel_virt_start,
             uint64_t kernel_virt_end,
             uint64_t trampoline_phys,
             uint64_t lapic_phys);

int vm_set_flags(uint64_t pml4_phys, uint64_t virt_addr, uint64_t set_bits, uint64_t clear_bits);
int vm_set_flags_local(uint64_t pml4_phys, uint64_t virt_addr, uint64_t set_bits, uint64_t clear_bits);

#endif
