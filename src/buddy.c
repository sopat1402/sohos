#include <stdint.h>
#include "../include/buddy.h"
#include "../include/memory_map.h"
#include "../include/memory_regions.h"
#include "../include/phys.h"
#include "../include/spinlock.h"

#define BUDDY_PAGE_SIZE        4096ull
#define BUDDY_MAX_ORDER        47u
#define BUDDY_META_MAX_EXTENTS 1024u

#define STATE_KIND_MASK  0x03u
#define STATE_FREE_HEAD  0x01u
#define STATE_ALLOC_HEAD 0x02u
#define STATE_ORDER_SHIFT 2u

static spinlock_t buddy_lock=SPINLOCK_INIT;

struct buddy_region {
    uint64_t first_pfn;
    uint64_t end_pfn;
    uint64_t first_state_index;
};

struct buddy_meta_extent {
    uint64_t first_state_index;
    uint64_t phys;
    uint64_t bytes;
};

struct buddy_link {
    uint64_t next_phys;
    uint64_t prev_phys;
};

static struct buddy_region regions[MAX_MEM_REGIONS];
static uint32_t region_count;

static struct buddy_meta_extent meta_extents[BUDDY_META_MAX_EXTENTS];
static uint32_t meta_extent_count;
static uint64_t meta_capacity_bytes;
static uint64_t tracked_pages;

static uint64_t free_list_heads[BUDDY_MAX_ORDER + 1];
static uint64_t free_pages;
static int ready;

static uint8_t make_state(uint8_t kind, uint8_t order) {
    return (uint8_t)((order << STATE_ORDER_SHIFT) | kind);
}

static uint8_t state_kind(uint8_t state) {
    return (uint8_t)(state & STATE_KIND_MASK);
}

static uint8_t state_order(uint8_t state) {
    return (uint8_t)(state >> STATE_ORDER_SHIFT);
}

static int find_page_index(uint64_t pfn, uint64_t *index, uint32_t *region_id) {
    uint32_t low = 0;
    uint32_t high = region_count;

    while (low < high) {
        uint32_t mid = low + (high - low) / 2;

        if (pfn < regions[mid].first_pfn) {
            high = mid;
        } else if (pfn >= regions[mid].end_pfn) {
            low = mid + 1;
        } else {
            *index = regions[mid].first_state_index + (pfn - regions[mid].first_pfn);
            if (region_id)
                *region_id = mid;
            return 1;
        }
    }
    return 0;
}

static uint8_t *state_pointer(uint64_t state_index) {
    uint32_t low = 0;
    uint32_t high = meta_extent_count;

    if (state_index >= tracked_pages)
        return 0;

    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        const struct buddy_meta_extent *extent = &meta_extents[mid];
        uint64_t extent_end = extent->first_state_index + extent->bytes;
        if (state_index < extent->first_state_index) {
            high = mid;
        } else if (state_index >= extent_end) {
            low = mid + 1;
        } else {
            uint64_t offset = state_index - extent->first_state_index;
            return (uint8_t *)phys_to_virt(extent->phys + offset);
        }
    }
    return 0;
}

static uint8_t get_page_state(uint64_t pfn) {
    uint64_t index;

    if (!find_page_index(pfn, &index, 0))
        return 0;
    uint8_t *state = state_pointer(index);
    return state ? *state : 0;
}

static int set_page_state(uint64_t pfn, uint8_t value) {
    uint64_t index;
    if (!find_page_index(pfn, &index, 0))
        return 0;
    uint8_t *state = state_pointer(index);
    if (!state)
        return 0;
    *state = value;
    return 1;
}

static struct buddy_link *link_at(uint64_t phys) {
    return (struct buddy_link *)phys_to_virt(phys);
}

static int region_for_block(uint64_t phys, uint8_t order, uint32_t *region_id) {
    if (order > BUDDY_MAX_ORDER)
        return 0;

    uint64_t block_bytes = BUDDY_PAGE_SIZE << order;
    uint64_t block_pages = 1ull << order;

    if (phys & (block_bytes - 1))
        return 0;

    uint64_t pfn = phys / BUDDY_PAGE_SIZE;

    for (uint32_t i = 0; i < region_count; i++) {
        if (pfn >= regions[i].first_pfn && pfn < regions[i].end_pfn && block_pages <= regions[i].end_pfn - pfn) {
            *region_id = i;
            return 1;
        }
    }
    return 0;
}

static int list_insert(uint64_t phys, uint8_t order) {
    uint64_t pfn = phys / BUDDY_PAGE_SIZE;
    uint8_t *state = 0;
    uint64_t index;

    if (order > BUDDY_MAX_ORDER || !find_page_index(pfn, &index, 0)) {
        return 0;
    }
    state = state_pointer(index);
    if (!state || *state != 0)
        return 0;
    struct buddy_link *node = link_at(phys);
    uint64_t old_head = free_list_heads[order];
    node->prev_phys = 0;
    node->next_phys = old_head;
    if (old_head)
        link_at(old_head)->prev_phys = phys;
    free_list_heads[order] = phys;
    *state = make_state(STATE_FREE_HEAD, order);
    return 1;
}

static int list_remove(uint64_t phys, uint8_t order) {
    uint64_t pfn = phys / BUDDY_PAGE_SIZE;
    uint64_t index;
    if (order > BUDDY_MAX_ORDER || !find_page_index(pfn, &index, 0)) {
        return 0;
    }
    uint8_t *state = state_pointer(index);
    if (!state || state_kind(*state) != STATE_FREE_HEAD || state_order(*state) != order) {
        return 0;
    }
    struct buddy_link *node = link_at(phys);
    uint64_t prev = node->prev_phys;
    uint64_t next = node->next_phys;
    if (prev)
        link_at(prev)->next_phys = next;
    else
        free_list_heads[order] = next;
    if (next)
        link_at(next)->prev_phys = prev;
    node->next_phys = 0;
    node->prev_phys = 0;
    *state = 0;
    return 1;
}

static uint8_t floor_log2(uint64_t value) {
    uint8_t order = 0;
    while (value > 1) {
        value >>= 1;
        order++;
    }
    return order;
}

static int add_free_interval(uint64_t first_pfn, uint64_t end_pfn) {
    while (first_pfn < end_pfn) {
        uint64_t remaining = end_pfn - first_pfn;
        uint8_t order = floor_log2(remaining);
        uint8_t alignment_order = first_pfn ? (uint8_t)__builtin_ctzll(first_pfn) : BUDDY_MAX_ORDER;
        if (order > alignment_order)
            order = alignment_order;
        if (order > BUDDY_MAX_ORDER)
            order = BUDDY_MAX_ORDER;
        if (!list_insert(first_pfn * BUDDY_PAGE_SIZE, order))
            return 0;
        uint64_t block_pages = 1ull << order;
        free_pages += block_pages;
        first_pfn += block_pages;
    }
    return 1;
}

static int allocate_metadata(void) {
    if (tracked_pages > UINT64_MAX - (BUDDY_PAGE_SIZE - 1))
        return 0;
    uint64_t pages_needed = (tracked_pages + BUDDY_PAGE_SIZE - 1) / BUDDY_PAGE_SIZE;
    uint64_t pages_left = pages_needed;
    uint64_t logical_offset = 0;
    for (uint32_t r = 0; r < region_count && pages_left; r++) {
        uint64_t pfn = regions[r].first_pfn;
        uint64_t end_pfn = regions[r].end_pfn;
        while (pfn < end_pfn && pages_left) {
            while (pfn < end_pfn && !frame_is_free(pfn))
                pfn++;
            if (pfn == end_pfn)
                break;
            uint64_t run_start = pfn;
            while (pfn < end_pfn && frame_is_free(pfn))
                pfn++;
            uint64_t run_pages = pfn - run_start;
            uint64_t take_pages = run_pages < pages_left ? run_pages : pages_left;
            if (meta_extent_count >= BUDDY_META_MAX_EXTENTS)
                return 0;
            struct buddy_meta_extent *extent = &meta_extents[meta_extent_count++];
            extent->first_state_index = logical_offset;
            extent->phys = run_start * BUDDY_PAGE_SIZE;
            extent->bytes = take_pages * BUDDY_PAGE_SIZE;
            for (uint64_t i = 0; i < take_pages; i++)
                mark_frame(run_start + i, 1);
            logical_offset += extent->bytes;
            pages_left -= take_pages;
        }
    }
    if (pages_left)
        return 0;
    meta_capacity_bytes = logical_offset;
    for (uint32_t e = 0; e < meta_extent_count; e++) {
        uint8_t *p = (uint8_t *)phys_to_virt(meta_extents[e].phys);
        for (uint64_t i = 0; i < meta_extents[e].bytes; i++)
            p[i] = 0;
    }
    return 1;
}

static int seed_from_bootstrap_bitmap(void) {
    for (uint32_t r = 0; r < region_count; r++) {
        uint64_t pfn = regions[r].first_pfn;
        uint64_t end_pfn = regions[r].end_pfn;
        while (pfn < end_pfn) {
            if (!frame_is_free(pfn)) {
                pfn++;
                continue;
            }
            uint64_t run_start = pfn;
            while (pfn < end_pfn && frame_is_free(pfn))
                pfn++;
            if (!add_free_interval(run_start, pfn))
                return 0;
        }
    }
    return 1;
}

static int release_reserved_page(uint64_t pfn) {
    if (get_page_state(pfn) != 0)
        return 0;
    uint64_t phys = pfn * BUDDY_PAGE_SIZE;
    uint8_t order = 0;
    while (order < BUDDY_MAX_ORDER) {
        uint64_t block_bytes = BUDDY_PAGE_SIZE << order;
        uint64_t buddy_phys = phys ^ block_bytes;
        uint32_t current_region;
        uint32_t buddy_region;
        if (!region_for_block(phys, order, &current_region) || !region_for_block(buddy_phys, order, &buddy_region) || current_region != buddy_region) {
            break;
        }
        uint8_t buddy_state = get_page_state(buddy_phys / BUDDY_PAGE_SIZE);
        if (state_kind(buddy_state) != STATE_FREE_HEAD || state_order(buddy_state) != order) {
            break;
        }
        if (!list_remove(buddy_phys, order))
            break;
        if (buddy_phys < phys)
            phys = buddy_phys;
        order++;
    }
    if (!list_insert(phys, order))
        return 0;
    free_pages++;
    return 1;
}

int buddy_init(uint64_t bootstrap_bitmap_phys,uint64_t bootstrap_bitmap_bytes) {
    if (ready || hhdm_offset != HHDM_BASE || !bootstrap_bitmap_bytes ||
        (bootstrap_bitmap_phys & (BUDDY_PAGE_SIZE - 1)) ||
        (bootstrap_bitmap_bytes & (BUDDY_PAGE_SIZE - 1)) ||
        bootstrap_bitmap_phys > UINT64_MAX - bootstrap_bitmap_bytes) {
        return 0;
    }

    region_count = memory_region_count();
    if (!region_count || region_count > MAX_MEM_REGIONS)
        return 0;
    tracked_pages = 0;
    meta_extent_count = 0;
    meta_capacity_bytes = 0;
    free_pages = 0;
    for (uint32_t i = 0; i <= BUDDY_MAX_ORDER; i++)
        free_list_heads[i] = 0;
    for (uint32_t i = 0; i < region_count; i++) {
        const struct mem_region *region = memory_region_get(i);

        if (!region ||
            region->end <= region->base ||
            (region->base & (BUDDY_PAGE_SIZE - 1)) ||
            (region->end & (BUDDY_PAGE_SIZE - 1))) {
            return 0;
        }

        uint64_t first_pfn = region->base / BUDDY_PAGE_SIZE;
        uint64_t end_pfn = region->end / BUDDY_PAGE_SIZE;
        uint64_t pages = end_pfn - first_pfn;

        if (tracked_pages > UINT64_MAX - pages)
            return 0;

        regions[i].first_pfn = first_pfn;
        regions[i].end_pfn = end_pfn;
        regions[i].first_state_index = tracked_pages;
        tracked_pages += pages;
    }

    if (!tracked_pages || !allocate_metadata())
        return 0;

    if (!seed_from_bootstrap_bitmap())
        return 0;

    uint64_t bitmap_first_pfn =
        bootstrap_bitmap_phys / BUDDY_PAGE_SIZE;
    uint64_t bitmap_page_count =
        bootstrap_bitmap_bytes / BUDDY_PAGE_SIZE;

    for (uint64_t i = 0; i < bitmap_page_count; i++) {
        if (!release_reserved_page(bitmap_first_pfn + i))
            return 0;
    }

    ready = 1;
    return 1;
}

static void zero_block(uint64_t phys, uint8_t order) {
    volatile uint64_t *words =
        (volatile uint64_t *)phys_to_virt(phys);
    uint64_t count =
        (BUDDY_PAGE_SIZE << order) / sizeof(uint64_t);

    for (uint64_t i = 0; i < count; i++)
        words[i] = 0;
}

uint64_t buddy_alloc_order(uint8_t order) {
    if (!ready || order > BUDDY_MAX_ORDER)
        return 0;
    uint64_t flags = spin_lock_irqsave(&buddy_lock);
    uint8_t found = order;

    while (found <= BUDDY_MAX_ORDER && !free_list_heads[found])
        found++;

    if (found > BUDDY_MAX_ORDER) {
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }

    uint64_t phys = free_list_heads[found];
    uint64_t requested_pages = 1ull << order;

    if (free_pages < requested_pages || !list_remove(phys, found)) {
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }

    while (found > order) {
        found--;

        uint64_t right_phys =
            phys + (BUDDY_PAGE_SIZE << found);

        if (!list_insert(right_phys, found)){
            spin_unlock_irqrestore(&buddy_lock, flags);
            return 0;
        }
    }

    if (!set_page_state(phys / BUDDY_PAGE_SIZE,make_state(STATE_ALLOC_HEAD, order))) {
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }

    free_pages -= requested_pages;
    spin_unlock_irqrestore(&buddy_lock, flags);
    zero_block(phys, order);
    return phys;
}

int buddy_free_order(uint64_t phys, uint8_t order) {
    if (!ready ||!phys ||order > BUDDY_MAX_ORDER) {
        return 0;
    }
    uint64_t flags = spin_lock_irqsave(&buddy_lock);
    uint32_t current_region;
    if (!region_for_block(phys, order, &current_region)){
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }
    uint64_t pfn = phys / BUDDY_PAGE_SIZE;
    uint8_t state = get_page_state(pfn);

    if (state_kind(state) != STATE_ALLOC_HEAD || state_order(state) != order) {
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }

    if (!set_page_state(pfn, 0)){
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }

    uint8_t merged_order = order;

    while (merged_order < BUDDY_MAX_ORDER) {
        uint64_t block_bytes =BUDDY_PAGE_SIZE << merged_order;
        uint64_t buddy_phys = phys ^ block_bytes;
        uint32_t left_region;
        uint32_t right_region;
        if (!region_for_block(phys, merged_order, &left_region) ||
            !region_for_block(buddy_phys, merged_order, &right_region) ||
            left_region != right_region) {
            break;
        }

        uint8_t buddy_state =
            get_page_state(buddy_phys / BUDDY_PAGE_SIZE);

        if (state_kind(buddy_state) != STATE_FREE_HEAD ||
            state_order(buddy_state) != merged_order) {
            break;
        }

        if (!list_remove(buddy_phys, merged_order))
            break;

        if (buddy_phys < phys)
            phys = buddy_phys;

        merged_order++;
    }

    if (!list_insert(phys, merged_order)){
        spin_unlock_irqrestore(&buddy_lock, flags);
        return 0;
    }
    free_pages += 1ull << order;
    spin_unlock_irqrestore(&buddy_lock, flags);
    return 1;
}

uint64_t buddy_available_pages(void) {
    return ready ? free_pages : 0;
}

