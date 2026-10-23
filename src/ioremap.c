#include <stdint.h>
#include <stddef.h>
#include "../include/virtual_manager.h"
#include "../include/vmalloc.h"
#include "../include/ioremap.h"

#define IO_PAGE_SIZE 4096ULL

#ifndef PTE_PWT
#define PTE_PWT (1ULL << 3)
#endif

#ifndef PTE_PCD
#define PTE_PCD (1ULL << 4)
#endif

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & 0x000FFFFFFFFFF000ull;
}

void *ioremap(uint64_t phys, uint64_t size) {
    if (size == 0 || phys + size < phys)
        return NULL;

    uint64_t offset = phys & (IO_PAGE_SIZE - 1);
    uint64_t base = phys - offset;
    uint64_t span = (offset + size + IO_PAGE_SIZE - 1) & ~(IO_PAGE_SIZE - 1);
    uint64_t virt = vmalloc_reserve(span);

    if (virt == 0)
        return NULL;

    uint64_t root = read_cr3();
    uint64_t mapped = 0;

    for (; mapped < span; mapped += IO_PAGE_SIZE) {
        uint64_t out = 0;

        if (vm_map_phys(root, virt + mapped, base + mapped, VM_READ | VM_WRITE, PTE_PCD | PTE_PWT, &out) != VM_MAP_OK)
            break;
    }

    if (mapped != span) {
        while (mapped > 0) {
            uint64_t unused = 0;

            mapped -= IO_PAGE_SIZE;
            (void)vm_unmap(root, virt + mapped, &unused);
        }

        (void)vmalloc_release(virt, span);
        return NULL;
    }

    return (void *)(virt + offset);
}

void iounmap(void *virt, uint64_t size) {
    if (virt == NULL || size == 0)
        return;

    uint64_t addr = (uint64_t)virt;
    uint64_t base = addr & ~(IO_PAGE_SIZE - 1);
    uint64_t span = ((addr & (IO_PAGE_SIZE - 1)) + size + IO_PAGE_SIZE - 1) & ~(IO_PAGE_SIZE - 1);
    uint64_t root = read_cr3();

    for (uint64_t off = 0; off < span; off += IO_PAGE_SIZE) {
        uint64_t unused = 0;

        (void)vm_unmap(root, base + off, &unused);
    }

    (void)vmalloc_release(base, span);
}
