#ifndef PHYS_H
#define PHYS_H

#include <stdint.h>

#define HHDM_BASE 0xFFFF800000000000ull
extern uint64_t hhdm_offset;
void phys_set_hhdm_offset(uint64_t offset);

static inline void *phys_to_virt(uint64_t phys){
    return (void *)(uintptr_t)(phys + hhdm_offset);
}

static inline uint64_t virt_to_phys(const void *virt){
    return (uint64_t)(uintptr_t)virt - hhdm_offset;
}

static inline uint64_t kernel_virt_to_phys(const void *virt){
    return (uint64_t)(uintptr_t)virt;
}

#endif
