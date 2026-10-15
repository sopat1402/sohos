#include <stdint.h>
#include <stddef.h>
#include "../include/spinlock.h"
#include "../include/buddy.h"
#include "../include/virtual_manager.h"
#include "../include/kmalloc.h"

#define PAGE_SIZE 4096ull
#define PAGE_MASK (PAGE_SIZE - 1)
#define VMALLOC_BASE 0xFFFFC00000000000
#define VMALLOC_END 0xFFFFC10000000000

enum vmalloc_error {
    VMALLOC_ERR_NONE = 0,
    VMALLOC_ERR_MAP_CONFLICT,
    VMALLOC_ERR_ROLLBACK_UNMAP,
    VMALLOC_ERR_ROLLBACK_BUDDY,
    VMALLOC_ERR_VFREE_UNMAP,
    VMALLOC_ERR_VFREE_BUDDY,
    VMALLOC_ERR_BAD_RECORD,
    VMALLOC_ERR_FREE_LIST_OVERLAP
};

struct free_region {
    uint64_t base;
    uint64_t end;
    struct free_region *next;
    uint32_t error;
    uint64_t error_addr;
    uint64_t error_phys;
};

struct virtual_allocator {
    struct free_region *head;
    uint64_t vmalloc_base;
    uint64_t vmalloc_end;
    uint64_t free_space;
    int initialized;
};

struct allocation_metadata {
    struct free_region *head;
    struct free_region *freeing_head;
    struct free_region *quarantine_head;
};

static struct virtual_allocator allocator = {
    .head = NULL,
    .vmalloc_base = VMALLOC_BASE,
    .vmalloc_end = VMALLOC_END,
    .free_space = 0,
    .initialized = 0
};

static struct allocation_metadata metadata = {0};

static struct free_region initial = {
    .base = VMALLOC_BASE,
    .end = VMALLOC_END,
    .next = NULL,
    .error = VMALLOC_ERR_NONE,
    .error_addr = 0,
    .error_phys = 0
};

static spinlock_t mem_lock = SPINLOCK_INIT;

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & 0x000FFFFFFFFFF000ull;
}

static void quarantine_locked(struct free_region *record, enum vmalloc_error error, uint64_t error_addr, uint64_t error_phys) {
    record->error = error;
    record->error_addr = error_addr;
    record->error_phys = error_phys;
    record->next = metadata.quarantine_head;
    metadata.quarantine_head = record;
}

static void quarantine_record(struct free_region *record, enum vmalloc_error error, uint64_t error_addr, uint64_t error_phys) {
    uint64_t irq_flags = spin_lock_irqsave(&mem_lock);
    quarantine_locked(record, error, error_addr, error_phys);
    spin_unlock_irqrestore(&mem_lock, irq_flags);
}

static int remove_record_locked(struct free_region **head, struct free_region *record) {
    struct free_region **link = head;

    while (*link != NULL && *link != record)
        link = &(*link)->next;

    if (*link == NULL)
        return 0;

    *link = record->next;
    record->next = NULL;
    return 1;
}

static void free_region_garbage(struct free_region *list) {
    while (list != NULL) {
        struct free_region *next = list->next;

        if (list != &initial)
            kfree(list);

        list = next;
    }
}

static int insert_free_region_locked(struct free_region *region, struct free_region **garbage_out) {
    struct free_region *prev = NULL;
    struct free_region *curr = allocator.head;
    struct free_region *merged;
    struct free_region *garbage = NULL;

    *garbage_out = NULL;

    if (!allocator.initialized ||
        region->base < allocator.vmalloc_base ||
        region->end > allocator.vmalloc_end ||
        region->base >= region->end ||
        ((region->base | region->end) & PAGE_MASK))
        return -1;

    while (curr != NULL && curr->base < region->base) {
        prev = curr;
        curr = curr->next;
    }

    if ((prev != NULL && prev->end > region->base) ||
        (curr != NULL && region->end > curr->base))
        return -1;

    if (prev != NULL && prev->end == region->base) {
        prev->end = region->end;
        region->next = garbage;
        garbage = region;
        merged = prev;
    } else {
        region->next = curr;

        if (prev != NULL)
            prev->next = region;
        else
            allocator.head = region;

        merged = region;
    }

    while (merged->next != NULL && merged->end == merged->next->base) {
        struct free_region *next = merged->next;

        merged->end = next->end;
        merged->next = next->next;
        next->next = garbage;
        garbage = next;
    }

    *garbage_out = garbage;
    return 0;
}

void vmalloc_init(void) {
    uint64_t irq_flags = spin_lock_irqsave(&mem_lock);

    if (!allocator.initialized) {
        initial.base = allocator.vmalloc_base;
        initial.end = allocator.vmalloc_end;
        initial.next = NULL;
        initial.error = VMALLOC_ERR_NONE;
        initial.error_addr = 0;
        initial.error_phys = 0;

        allocator.head = &initial;
        allocator.free_space = allocator.vmalloc_end - allocator.vmalloc_base;
        allocator.initialized = 1;
    }

    spin_unlock_irqrestore(&mem_lock, irq_flags);
}

uint64_t size_to_pages(uint64_t size) {
    return size / PAGE_SIZE + (size % PAGE_SIZE != 0);
}

uint64_t vmalloc(uint64_t size) {
    if (size == 0)
        return 0;

    uint64_t pages = size_to_pages(size);

    if (pages > UINT64_MAX / PAGE_SIZE)
        return 0;

    uint64_t alloc_size = pages * PAGE_SIZE;

    if (alloc_size > VMALLOC_END - VMALLOC_BASE)
        return 0;

    struct free_region *record = kmalloc(sizeof(*record));

    if (record == NULL)
        return 0;

    record->base = 0;
    record->end = 0;
    record->next = NULL;
    record->error = VMALLOC_ERR_NONE;
    record->error_addr = 0;
    record->error_phys = 0;

    uint64_t addr = 0;
    struct free_region *detached = NULL;
    uint64_t irq_flags = spin_lock_irqsave(&mem_lock);

    if (allocator.initialized && allocator.free_space >= alloc_size) {
        struct free_region **link = &allocator.head;

        while (*link != NULL) {
            struct free_region *curr = *link;
            uint64_t available = curr->end - curr->base;

            if (available >= alloc_size) {
                addr = curr->base;

                if (available == alloc_size) {
                    *link = curr->next;
                    detached = curr;
                    detached->next = NULL;
                } else {
                    curr->base += alloc_size;
                }

                allocator.free_space -= alloc_size;
                break;
            }

            link = &curr->next;
        }
    }

    spin_unlock_irqrestore(&mem_lock, irq_flags);

    if (addr == 0) {
        kfree(record);
        return 0;
    }

    if (detached != NULL && detached != &initial)
        kfree(detached);

    uint64_t root = read_cr3();
    uint64_t mapped = 0;
    enum vm_map_status failure_status = VM_MAP_OK;

    for (; mapped < pages; mapped++) {
        uint64_t out = 0;
        uint64_t virt = addr + mapped * PAGE_SIZE;

        failure_status = vm_map(root, virt, VM_READ | VM_WRITE, &out);

        if (failure_status != VM_MAP_OK)
            break;
    }

    if (mapped == pages) {
        record->base = addr;
        record->end = addr + alloc_size;

        irq_flags = spin_lock_irqsave(&mem_lock);
        record->next = metadata.head;
        metadata.head = record;
        spin_unlock_irqrestore(&mem_lock, irq_flags);

        return addr;
    }

    uint64_t map_fail_addr = addr + mapped * PAGE_SIZE;

    while (mapped > 0) {
        uint64_t virt = addr + (mapped - 1) * PAGE_SIZE;
        uint64_t phys = 0;

        if (vm_unmap(root, virt, &phys) != VM_UNMAP_OK || phys == 0) {
            record->base = addr;
            record->end = addr + alloc_size;
            quarantine_record(record, VMALLOC_ERR_ROLLBACK_UNMAP, virt, phys);
            return 0;
        }

        if (buddy_free_order(phys, 0) != 1) {
            record->base = addr;
            record->end = addr + alloc_size;
            quarantine_record(record, VMALLOC_ERR_ROLLBACK_BUDDY, virt, phys);
            return 0;
        }

        mapped--;
    }

    if (failure_status == VM_MAP_ALREADY_MAPPED) {
        record->base = addr;
        record->end = addr + alloc_size;
        quarantine_record(record, VMALLOC_ERR_MAP_CONFLICT, map_fail_addr, 0);
        return 0;
    }

    record->base = addr;
    record->end = addr + alloc_size;

    struct free_region *garbage = NULL;

    irq_flags = spin_lock_irqsave(&mem_lock);

    record->error = VMALLOC_ERR_NONE;
    record->error_addr = 0;
    record->error_phys = 0;

    if (insert_free_region_locked(record, &garbage) == 0) {
        allocator.free_space += alloc_size;
    } else {
        quarantine_locked(record, VMALLOC_ERR_FREE_LIST_OVERLAP, addr, 0);
    }

    spin_unlock_irqrestore(&mem_lock, irq_flags);

    free_region_garbage(garbage);
    return 0;
}

int vfree(uintptr_t base) {
    if (base == 0 ||
        (base & PAGE_MASK) != 0 ||
        (uint64_t)base < VMALLOC_BASE ||
        (uint64_t)base >= VMALLOC_END)
        return -1;

    uint64_t irq_flags = spin_lock_irqsave(&mem_lock);
    struct free_region **link = &metadata.head;

    while (*link != NULL && (*link)->base != (uint64_t)base)
        link = &(*link)->next;

    if (*link == NULL) {
        spin_unlock_irqrestore(&mem_lock, irq_flags);
        return -1;
    }

    struct free_region *record = *link;

    if (record->base < allocator.vmalloc_base ||
        record->end > allocator.vmalloc_end ||
        record->end <= record->base ||
        ((record->base | record->end) & PAGE_MASK)) {
        *link = record->next;
        record->next = NULL;
        quarantine_locked(record, VMALLOC_ERR_BAD_RECORD, record->base, 0);
        spin_unlock_irqrestore(&mem_lock, irq_flags);
        return -1;
    }

    *link = record->next;
    record->next = metadata.freeing_head;
    metadata.freeing_head = record;

    spin_unlock_irqrestore(&mem_lock, irq_flags);

    uint64_t alloc_size = record->end - record->base;
    uint64_t pages = alloc_size / PAGE_SIZE;
    uint64_t root = read_cr3();

    for (uint64_t i = 0; i < pages; i++) {
        uint64_t virt = record->base + i * PAGE_SIZE;
        uint64_t phys = 0;

        if (vm_unmap(root, virt, &phys) != VM_UNMAP_OK || phys == 0) {
            irq_flags = spin_lock_irqsave(&mem_lock);

            remove_record_locked(&metadata.freeing_head, record);
            quarantine_locked(record, VMALLOC_ERR_VFREE_UNMAP, virt, phys);

            spin_unlock_irqrestore(&mem_lock, irq_flags);
            return -1;
        }

        if (buddy_free_order(phys, 0) != 1) {
            irq_flags = spin_lock_irqsave(&mem_lock);

            remove_record_locked(&metadata.freeing_head, record);
            quarantine_locked(record, VMALLOC_ERR_VFREE_BUDDY, virt, phys);

            spin_unlock_irqrestore(&mem_lock, irq_flags);
            return -1;
        }
    }

    struct free_region *garbage = NULL;

    irq_flags = spin_lock_irqsave(&mem_lock);

    if (!remove_record_locked(&metadata.freeing_head, record)) {
        quarantine_locked(record, VMALLOC_ERR_BAD_RECORD, record->base, 0);
        spin_unlock_irqrestore(&mem_lock, irq_flags);
        return -1;
    }

    record->next = NULL;
    record->error = VMALLOC_ERR_NONE;
    record->error_addr = 0;
    record->error_phys = 0;

    if (insert_free_region_locked(record, &garbage) != 0) {
        quarantine_locked(record, VMALLOC_ERR_FREE_LIST_OVERLAP, record->base, 0);
        spin_unlock_irqrestore(&mem_lock, irq_flags);
        return -1;
    }

    allocator.free_space += alloc_size;

    spin_unlock_irqrestore(&mem_lock, irq_flags);

    free_region_garbage(garbage);
    return 0;
}
