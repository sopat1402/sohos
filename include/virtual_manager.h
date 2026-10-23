#ifndef SOHOS_VIRTUAL_MANAGER_H
#define SOHOS_VIRTUAL_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VM_READ   (1ULL << 0)
#define VM_WRITE  (1ULL << 1)
#define VM_USER   (1ULL << 2)
#define VM_EXEC   (1ULL << 3)
#define VM_LARGE  (1ULL << 4)

#define VM_MAP_REQUEST_FLAGS (VM_READ | VM_WRITE | VM_USER | VM_EXEC)

enum vm_walk_status {
    VM_WALK_OK = 0,
    VM_WALK_NONCANONICAL,
    VM_WALK_NOT_PRESENT_PML4,
    VM_WALK_NOT_PRESENT_PDPT,
    VM_WALK_NOT_PRESENT_PD,
    VM_WALK_NOT_PRESENT_PT,
    VM_WALK_BAD_ROOT,
    VM_WALK_BAD_ARGUMENT,
    VM_WALK_BAD_TABLE,
    VM_WALK_INVALID_ENTRY
};

enum vm_map_status {
    VM_MAP_OK = 0,
    VM_MAP_ALREADY_MAPPED,
    VM_MAP_BAD_ARGUMENT,
    VM_MAP_BAD_FLAGS,
    VM_MAP_NONCANONICAL,
    VM_MAP_BAD_ROOT,
    VM_MAP_NO_MEMORY,
    VM_MAP_BAD_TABLE,
    VM_MAP_INVALID_ENTRY,
    VM_MAP_PERMISSION_CONFLICT,
    VM_MAP_RETRY
};

enum vm_unmap_status {
    VM_UNMAP_OK = 0,
    VM_UNMAP_BAD_ARGUMENT,
    VM_UNMAP_NONCANONICAL,
    VM_UNMAP_BAD_ROOT,
    VM_UNMAP_NOT_MAPPED,
    VM_UNMAP_HUGE_MAPPING,
    VM_UNMAP_BAD_TABLE,
    VM_UNMAP_INVALID_ENTRY
};

enum vm_walk_status vm_translate(uint64_t root_phys, uintptr_t virt_addr,uint64_t *out_phys, uint64_t *out_flags);

enum vm_map_status vm_map(uint64_t root_phys, uintptr_t virt_addr,uint64_t flags, uint64_t *out_phys);

enum vm_unmap_status vm_unmap(uint64_t root_phys, uintptr_t virt_addr,uint64_t *out_phys);
enum vm_map_status vm_map_phys(uint64_t root_phys, uintptr_t virt_addr, uint64_t phys, uint64_t flags, uint64_t pte_extra, uint64_t *out_phys);
#ifdef __cplusplus
}
#endif

#endif
