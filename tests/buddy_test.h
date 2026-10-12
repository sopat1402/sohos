#ifndef TEST
#define TEST

#include <stdint.h>
#include "../include/phys.h"
#include "../include/buddy.h"
#include "../include/display.h"

static int buddy_test_block_is_zero(uint64_t phys, uint64_t bytes)
{
    volatile const uint8_t *p =
        (volatile const uint8_t *)phys_to_virt(phys);

    for (uint64_t i = 0; i < bytes; i++) {
        if (p[i] != 0)
            return 0;
    }

    return 1;
}

static int buddy_test_page_is_inside(uint64_t page,uint64_t block,uint64_t block_bytes)
{
    return page >= block && (page - block) < block_bytes;
}

static int buddy_self_test(void)
{
    const uint64_t page_size = 4096ULL;
    const uint64_t order2_bytes = page_size << 2; /* 4 pages */
    const uint64_t pages_before = buddy_available_pages();

    uint64_t p0 = 0;
    uint64_t p1 = 0;
    uint64_t p2 = 0;

    if (pages_before < 6) {
        print("Buddy test: not enough free pages\n");
        return 0;
    }

    p0 = buddy_alloc_order(0);
    if (p0 == 0) {
        print("Buddy test: order-0 allocation failed\n");
        return 0;
    }

    p1 = buddy_alloc_order(0);
    if (p1 == 0) {
        (void)buddy_free_order(p0, 0);
        print("Buddy test: second order-0 allocation failed\n");
        return 0;
    }

    /* If these overlap, don't try to free possibly duplicated allocations. */
    if (p0 == p1) {
        print("Buddy test: duplicate allocation\n");
        return 0;
    }

    p2 = buddy_alloc_order(2);
    if (p2 == 0) {
        (void)buddy_free_order(p1, 0);
        (void)buddy_free_order(p0, 0);
        print("Buddy test: order-2 allocation failed\n");
        return 0;
    }

    /* Check overlap before cleanup; an overlap signals allocator corruption. */
    if (buddy_test_page_is_inside(p0, p2, order2_bytes) ||
        buddy_test_page_is_inside(p1, p2, order2_bytes)) {
        print("Buddy test: allocations overlap\n");
        return 0;
    }

    if ((p0 & (page_size - 1)) != 0 ||
        (p1 & (page_size - 1)) != 0 ||
        (p2 & (order2_bytes - 1)) != 0) {
        print("Buddy test: allocation alignment is wrong\n");
        return 0;
    }

    if (buddy_available_pages() != pages_before - 6) {
        print("Buddy test: free-page count after allocation is wrong\n");
        return 0;
    }

    if (!buddy_test_block_is_zero(p0, page_size) ||
        !buddy_test_block_is_zero(p1, page_size) ||
        !buddy_test_block_is_zero(p2, order2_bytes)) {
        print("Buddy test: allocated memory was not zeroed\n");
        return 0;
    }

    if (buddy_free_order(p2, 2) != 1 ||
        buddy_free_order(p1, 0) != 1 ||
        buddy_free_order(p0, 0) != 1) {
        print("Buddy test: free failed\n");
        return 0;
    }

    if (buddy_available_pages() != pages_before) {
        print("Buddy test: free-page count did not recover\n");
        return 0;
    }

    print("Buddy test: PASS\n");
    return 1;
}
#endif
