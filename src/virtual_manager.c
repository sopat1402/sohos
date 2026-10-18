#include <stdint.h>
#include <stddef.h>
#include "../include/phys.h"
#include "../include/spinlock.h"
#include "../include/buddy.h"
#include "../include/tlb.h"

static spinlock_t map_lock = SPINLOCK_INIT;

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_PS       (1ULL << 7)
#define PTE_NX       (1ULL << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PTE_1G_MASK   0x000FFFFFC0000000ULL
#define PTE_2M_MASK   0x000FFFFFFFE00000ULL

#define VM_READ      (1ULL << 0)
#define VM_WRITE     (1ULL << 1)
#define VM_USER      (1ULL << 2)
#define VM_EXEC      (1ULL << 3)
#define VM_LARGE     (1ULL << 4)

enum vm_walk_status {
    VM_WALK_OK = 0,
    VM_WALK_NONCANONICAL = 1,
    VM_WALK_NOT_PRESENT_PML4 = 2,
    VM_WALK_NOT_PRESENT_PDPT = 3,
    VM_WALK_NOT_PRESENT_PD = 4,
    VM_WALK_NOT_PRESENT_PT = 5,
    VM_WALK_BAD_ROOT = 6,
    VM_WALK_BAD_ARGUMENT = 7,
    VM_WALK_BAD_TABLE = 8,
    VM_WALK_INVALID_ENTRY = 9
};

static int canonical_addr(uintptr_t addr) {
    uint64_t top = (uint64_t)addr >> 47;
    return top == 0 || top == 0x1FFFFULL;
}

static uint64_t vm_permissions(int writable, int user, int nx, int large) {
    return VM_READ |
           (writable ? VM_WRITE : 0) |
           (user ? VM_USER : 0) |
           (!nx ? VM_EXEC : 0) |
           (large ? VM_LARGE : 0);
}

static enum vm_walk_status vm_translate_unlocked(uint64_t root_phys, uintptr_t virt_addr, uint64_t *out_phys, uint64_t *out_flags) {
    if (!out_phys || !out_flags)
        return VM_WALK_BAD_ARGUMENT;

    *out_phys = 0;
    *out_flags = 0;

    if (!canonical_addr(virt_addr))
        return VM_WALK_NONCANONICAL;

    if (root_phys & ~PTE_ADDR_MASK)
        return VM_WALK_BAD_ROOT;

    uint64_t *pml4 = (uint64_t *)phys_to_virt(root_phys);
    if (!pml4)
        return VM_WALK_BAD_TABLE;

    uint64_t entry = pml4[(virt_addr >> 39) & 0x1FF];

    if (!(entry & PTE_PRESENT))
        return VM_WALK_NOT_PRESENT_PML4;

    if (entry & PTE_PS)
        return VM_WALK_INVALID_ENTRY;

    int effective_rw = (entry & PTE_WRITABLE) != 0;
    int effective_user = (entry & PTE_USER) != 0;
    int effective_nx = (entry & PTE_NX) != 0;

    uint64_t *pdpt = (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);
    if (!pdpt)
        return VM_WALK_BAD_TABLE;

    entry = pdpt[(virt_addr >> 30) & 0x1FF];

    if (!(entry & PTE_PRESENT))
        return VM_WALK_NOT_PRESENT_PDPT;

    effective_rw &= (entry & PTE_WRITABLE) != 0;
    effective_user &= (entry & PTE_USER) != 0;
    effective_nx |= (entry & PTE_NX) != 0;

    if (entry & PTE_PS) {
        *out_phys = (entry & PTE_1G_MASK) |
                    ((uint64_t)virt_addr & 0x3FFFFFFFULL);

        *out_flags = vm_permissions(effective_rw, effective_user, effective_nx, 1);
        return VM_WALK_OK;
    }

    uint64_t *pd = (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);
    if (!pd)
        return VM_WALK_BAD_TABLE;

    entry = pd[(virt_addr >> 21) & 0x1FF];

    if (!(entry & PTE_PRESENT))
        return VM_WALK_NOT_PRESENT_PD;

    effective_rw &= (entry & PTE_WRITABLE) != 0;
    effective_user &= (entry & PTE_USER) != 0;
    effective_nx |= (entry & PTE_NX) != 0;

    if (entry & PTE_PS) {
        *out_phys = (entry & PTE_2M_MASK) |
                    ((uint64_t)virt_addr & 0x1FFFFFULL);

        *out_flags = vm_permissions(effective_rw, effective_user, effective_nx, 1);
        return VM_WALK_OK;
    }

    uint64_t *pt = (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);
    if (!pt)
        return VM_WALK_BAD_TABLE;

    entry = pt[(virt_addr >> 12) & 0x1FF];

    if (!(entry & PTE_PRESENT))
        return VM_WALK_NOT_PRESENT_PT;

    effective_rw &= (entry & PTE_WRITABLE) != 0;
    effective_user &= (entry & PTE_USER) != 0;
    effective_nx |= (entry & PTE_NX) != 0;

    *out_phys = (entry & PTE_ADDR_MASK) |
                ((uint64_t)virt_addr & 0xFFFULL);

    *out_flags = vm_permissions(effective_rw, effective_user, effective_nx, 0);
    return VM_WALK_OK;
}

enum vm_walk_status vm_translate(uint64_t root_phys, uintptr_t virt_addr, uint64_t *out_phys, uint64_t *out_flags) {
    if (!out_phys || !out_flags)
        return VM_WALK_BAD_ARGUMENT;

    uint64_t irq_flags = spin_lock_irqsave(&map_lock);
    enum vm_walk_status status = vm_translate_unlocked(root_phys, virt_addr, out_phys, out_flags);
    spin_unlock_irqrestore(&map_lock, irq_flags);

    return status;
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void vm_invlpg(uintptr_t virt_addr) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
}

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

static uint8_t vm_map_tables_needed(enum vm_walk_status status) {
    switch (status) {
        case VM_WALK_NOT_PRESENT_PML4: return 3;
        case VM_WALK_NOT_PRESENT_PDPT: return 2;
        case VM_WALK_NOT_PRESENT_PD:   return 1;
        case VM_WALK_NOT_PRESENT_PT:   return 0;
        default: return UINT8_MAX;
    }
}

static enum vm_map_status vm_map_walk_error(enum vm_walk_status status) {
    switch (status) {
        case VM_WALK_NONCANONICAL: return VM_MAP_NONCANONICAL;
        case VM_WALK_BAD_ROOT:     return VM_MAP_BAD_ROOT;
        case VM_WALK_BAD_ARGUMENT: return VM_MAP_BAD_ARGUMENT;
        case VM_WALK_BAD_TABLE:    return VM_MAP_BAD_TABLE;
        case VM_WALK_INVALID_ENTRY:return VM_MAP_INVALID_ENTRY;
        default:                   return VM_MAP_BAD_TABLE;
    }
}

static int vm_entry_allows(uint64_t entry, uint64_t flags) {
    if ((flags & VM_WRITE) && !(entry & PTE_WRITABLE))
        return 0;
    if ((flags & VM_USER) && !(entry & PTE_USER))
        return 0;
    if ((flags & VM_EXEC) && (entry & PTE_NX))
        return 0;
    return 1;
}

static void vm_map_free_staged(uint64_t data_phys, uint64_t tables[3], uint8_t count) {
    if (data_phys)
        (void)buddy_free_order(data_phys, 0);

    for (uint8_t i = 0; i < count; i++) {
        if (tables[i])
            (void)buddy_free_order(tables[i], 0);
    }
}

enum vm_map_status vm_map(uint64_t root_phys, uintptr_t virt_addr, uint64_t flags, uint64_t *out_phys) {
    uint64_t lock_flags, existing_phys = 0, existing_flags = 0;
    uint64_t data_phys = 0, new_tables[3] = {0};
    uint64_t e4 = 0, e3 = 0, e2 = 0;
    uint64_t *pml4 = NULL, *pdpt = NULL, *pd = NULL, *pt = NULL;
    uint64_t leaf_entry, table_entry_flags;
    enum vm_walk_status walk;
    enum vm_map_status failure;
    uint8_t table_count, allocated_tables = 0, needed_now;
    uint64_t i4 = (virt_addr >> 39) & 0x1FF;
    uint64_t i3 = (virt_addr >> 30) & 0x1FF;
    uint64_t i2 = (virt_addr >> 21) & 0x1FF;
    uint64_t i1 = (virt_addr >> 12) & 0x1FF;

    if (!out_phys)
        return VM_MAP_BAD_ARGUMENT;

    *out_phys = 0;

    if (!root_phys || (root_phys & ~PTE_ADDR_MASK))
        return VM_MAP_BAD_ROOT;

    if (!canonical_addr(virt_addr))
        return VM_MAP_NONCANONICAL;

    if (virt_addr & 0xFFFULL)
        return VM_MAP_BAD_ARGUMENT;

    if (!(flags & VM_READ) || (flags & ~(VM_READ | VM_WRITE | VM_USER | VM_EXEC)))
        return VM_MAP_BAD_FLAGS;

    if ((flags & VM_WRITE) && (flags & VM_EXEC))
        return VM_MAP_BAD_FLAGS;

    lock_flags = spin_lock_irqsave(&map_lock);
    walk = vm_translate_unlocked(root_phys, virt_addr, &existing_phys, &existing_flags);

    if (walk == VM_WALK_OK) {
        *out_phys = existing_phys;
        spin_unlock_irqrestore(&map_lock, lock_flags);
        return VM_MAP_ALREADY_MAPPED;
    }

    table_count = vm_map_tables_needed(walk);

    if (table_count == UINT8_MAX) {
        failure = vm_map_walk_error(walk);
        spin_unlock_irqrestore(&map_lock, lock_flags);
        return failure;
    }

    spin_unlock_irqrestore(&map_lock, lock_flags);

    data_phys = buddy_alloc_order(0);

    if (!data_phys)
        return VM_MAP_NO_MEMORY;

    for (uint8_t i = 0; i < table_count; i++) {
        new_tables[i] = buddy_alloc_order(0);

        if (!new_tables[i]) {
            vm_map_free_staged(data_phys, new_tables, allocated_tables);
            return VM_MAP_NO_MEMORY;
        }

        allocated_tables++;
    }

    lock_flags = spin_lock_irqsave(&map_lock);
    walk = vm_translate_unlocked(root_phys, virt_addr, &existing_phys, &existing_flags);

    if (walk == VM_WALK_OK) {
        *out_phys = existing_phys;
        spin_unlock_irqrestore(&map_lock, lock_flags);
        vm_map_free_staged(data_phys, new_tables, allocated_tables);
        return VM_MAP_ALREADY_MAPPED;
    }

    needed_now = vm_map_tables_needed(walk);

    if (needed_now == UINT8_MAX) {
        failure = vm_map_walk_error(walk);
        goto fail_locked;
    }

    if (needed_now > allocated_tables) {
        failure = VM_MAP_RETRY;
        goto fail_locked;
    }

    pml4 = (uint64_t *)phys_to_virt(root_phys);

    if (!pml4) {
        failure = VM_MAP_BAD_TABLE;
        goto fail_locked;
    }

    if (walk != VM_WALK_NOT_PRESENT_PML4) {
        e4 = __atomic_load_n(&pml4[i4], __ATOMIC_ACQUIRE);

        if (!(e4 & PTE_PRESENT) || (e4 & PTE_PS)) {
            failure = VM_MAP_INVALID_ENTRY;
            goto fail_locked;
        }

        if (!vm_entry_allows(e4, flags)) {
            failure = VM_MAP_PERMISSION_CONFLICT;
            goto fail_locked;
        }

        pdpt = (uint64_t *)phys_to_virt(e4 & PTE_ADDR_MASK);

        if (!pdpt) {
            failure = VM_MAP_BAD_TABLE;
            goto fail_locked;
        }
    }

    if (walk == VM_WALK_NOT_PRESENT_PD ||
        walk == VM_WALK_NOT_PRESENT_PT) {
        e3 = __atomic_load_n(&pdpt[i3], __ATOMIC_ACQUIRE);

        if (!(e3 & PTE_PRESENT) || (e3 & PTE_PS)) {
            failure = VM_MAP_INVALID_ENTRY;
            goto fail_locked;
        }

        if (!vm_entry_allows(e3, flags)) {
            failure = VM_MAP_PERMISSION_CONFLICT;
            goto fail_locked;
        }

        pd = (uint64_t *)phys_to_virt(e3 & PTE_ADDR_MASK);

        if (!pd) {
            failure = VM_MAP_BAD_TABLE;
            goto fail_locked;
        }
    }

    if (walk == VM_WALK_NOT_PRESENT_PT) {
        e2 = __atomic_load_n(&pd[i2], __ATOMIC_ACQUIRE);

        if (!(e2 & PTE_PRESENT) || (e2 & PTE_PS)) {
            failure = VM_MAP_INVALID_ENTRY;
            goto fail_locked;
        }

        if (!vm_entry_allows(e2, flags)) {
            failure = VM_MAP_PERMISSION_CONFLICT;
            goto fail_locked;
        }

        pt = (uint64_t *)phys_to_virt(e2 & PTE_ADDR_MASK);

        if (!pt) {
            failure = VM_MAP_BAD_TABLE;
            goto fail_locked;
        }
    }

    switch (walk) {
        case VM_WALK_NOT_PRESENT_PML4:
            pdpt = (uint64_t *)phys_to_virt(new_tables[0]);
            pd = (uint64_t *)phys_to_virt(new_tables[1]);
            pt = (uint64_t *)phys_to_virt(new_tables[2]);
            break;

        case VM_WALK_NOT_PRESENT_PDPT:
            pd = (uint64_t *)phys_to_virt(new_tables[0]);
            pt = (uint64_t *)phys_to_virt(new_tables[1]);
            break;

        case VM_WALK_NOT_PRESENT_PD:
            pt = (uint64_t *)phys_to_virt(new_tables[0]);
            break;

        case VM_WALK_NOT_PRESENT_PT:
            break;

        default:
            failure = VM_MAP_INVALID_ENTRY;
            goto fail_locked;
    }

    if (!pdpt || !pd || !pt) {
        failure = VM_MAP_BAD_TABLE;
        goto fail_locked;
    }

    table_entry_flags = PTE_PRESENT | PTE_WRITABLE;
    if (flags & VM_USER) {
        table_entry_flags |= PTE_USER;
    }

    leaf_entry = (data_phys & PTE_ADDR_MASK) | PTE_PRESENT;
    if (flags & VM_WRITE)
        leaf_entry |= PTE_WRITABLE;

    if (flags & VM_USER)
        leaf_entry |= PTE_USER;

    if (!(flags & VM_EXEC))
        leaf_entry |= PTE_NX;

    switch (walk) {
        case VM_WALK_NOT_PRESENT_PML4:
            __atomic_store_n(&pt[i1], leaf_entry, __ATOMIC_RELAXED);
            pd[i2] = new_tables[2] | table_entry_flags;
            pdpt[i3] = new_tables[1] | table_entry_flags;
            __atomic_store_n(&pml4[i4], new_tables[0] | table_entry_flags, __ATOMIC_RELEASE);
            break;

        case VM_WALK_NOT_PRESENT_PDPT:
            __atomic_store_n(&pt[i1], leaf_entry, __ATOMIC_RELAXED);
            pd[i2] = new_tables[1] | table_entry_flags;
            __atomic_store_n(&pdpt[i3], new_tables[0] | table_entry_flags, __ATOMIC_RELEASE);
            break;

        case VM_WALK_NOT_PRESENT_PD:
            __atomic_store_n(&pt[i1], leaf_entry, __ATOMIC_RELAXED);
            __atomic_store_n(&pd[i2], new_tables[0] | table_entry_flags, __ATOMIC_RELEASE);
            break;

        case VM_WALK_NOT_PRESENT_PT:
            __atomic_store_n(&pt[i1], leaf_entry, __ATOMIC_RELEASE);
            break;

        default:
            failure = VM_MAP_INVALID_ENTRY;
            goto fail_locked;
    }

    *out_phys = data_phys;


    spin_unlock_irqrestore(&map_lock, lock_flags);
    tlb_shootdown();
    for (uint8_t i = needed_now; i < allocated_tables; i++)
        (void)buddy_free_order(new_tables[i], 0);

    return VM_MAP_OK;

fail_locked:
    spin_unlock_irqrestore(&map_lock, lock_flags);
    vm_map_free_staged(data_phys, new_tables, allocated_tables);
    return failure;
}

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

enum vm_unmap_status vm_unmap(uint64_t root_phys, uintptr_t virt_addr, uint64_t *out_phys) {
    uint64_t lock_flags, entry, old_entry;
    uint64_t *pml4, *pdpt, *pd, *pt;
    enum vm_unmap_status status = VM_UNMAP_OK;

    if (!out_phys)
        return VM_UNMAP_BAD_ARGUMENT;

    *out_phys = 0;

    if (!root_phys || (root_phys & ~PTE_ADDR_MASK))
        return VM_UNMAP_BAD_ROOT;

    if (!canonical_addr(virt_addr))
        return VM_UNMAP_NONCANONICAL;

    if (virt_addr & 0xFFFULL)
        return VM_UNMAP_BAD_ARGUMENT;

    lock_flags = spin_lock_irqsave(&map_lock);

    pml4 = (uint64_t *)phys_to_virt(root_phys);

    if (!pml4) {
        status = VM_UNMAP_BAD_TABLE;
        goto done;
    }

    entry = __atomic_load_n(&pml4[(virt_addr >> 39) & 0x1FF], __ATOMIC_ACQUIRE);

    if (!(entry & PTE_PRESENT)) {
        status = VM_UNMAP_NOT_MAPPED;
        goto done;
    }

    if (entry & PTE_PS) {
        status = VM_UNMAP_INVALID_ENTRY;
        goto done;
    }

    pdpt = (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);

    if (!pdpt) {
        status = VM_UNMAP_BAD_TABLE;
        goto done;
    }

    entry = __atomic_load_n(&pdpt[(virt_addr >> 30) & 0x1FF], __ATOMIC_ACQUIRE);

    if (!(entry & PTE_PRESENT)) {
        status = VM_UNMAP_NOT_MAPPED;
        goto done;
    }

    if (entry & PTE_PS) {
        status = VM_UNMAP_HUGE_MAPPING;
        goto done;
    }

    pd = (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);

    if (!pd) {
        status = VM_UNMAP_BAD_TABLE;
        goto done;
    }

    entry = __atomic_load_n(&pd[(virt_addr >> 21) & 0x1FF], __ATOMIC_ACQUIRE);

    if (!(entry & PTE_PRESENT)) {
        status = VM_UNMAP_NOT_MAPPED;
        goto done;
    }

    if (entry & PTE_PS) {
        status = VM_UNMAP_HUGE_MAPPING;
        goto done;
    }

    pt = (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);

    if (!pt) {
        status = VM_UNMAP_BAD_TABLE;
        goto done;
    }

    uint64_t *pte = &pt[(virt_addr >> 12) & 0x1FF];

    old_entry = __atomic_exchange_n(pte, 0, __ATOMIC_ACQ_REL);

    if (!(old_entry & PTE_PRESENT)) {
        status = VM_UNMAP_NOT_MAPPED;
        goto done;
    }

    *out_phys = old_entry & PTE_ADDR_MASK;


done:
    spin_unlock_irqrestore(&map_lock, lock_flags);
    tlb_shootdown();
    return status;
}
