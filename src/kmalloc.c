#include <stdint.h>
#include "../include/kmalloc.h"
#include "../include/slub.h"
#include "../include/buddy.h"
#include "../include/memory_regions.h"
#include "../include/phys.h"

#define KM_PAGE_SIZE   4096ull
#define KM_CLASS_COUNT 8u
#define KM_MAX_SMALL   2048u
#define KM_LARGE_FLAG  0x80u
#define KM_ORDER_MASK  0x7fu
#define KM_ORDER_LIMIT 47u

static const size_t class_sizes[KM_CLASS_COUNT] = {16, 32, 64, 128, 256, 512, 1024, 2048};
static const uint8_t class_orders[KM_CLASS_COUNT] = {0, 0, 0, 0, 0, 2, 2, 2};

static slub_cache_t caches[KM_CLASS_COUNT];
static uint8_t *page_meta;
static uint64_t meta_pages;
static int km_ready;

static int class_for(size_t size) {
    for (uint32_t i = 0; i < KM_CLASS_COUNT; i++) {
        if (class_sizes[i] >= size)
            return (int)i;
    }
    return -1;
}

static int order_for(size_t size, uint8_t *order) {
    if ((uint64_t)size > (KM_PAGE_SIZE << KM_ORDER_LIMIT))
        return 0;
    uint64_t pages = ((uint64_t)size + KM_PAGE_SIZE - 1) / KM_PAGE_SIZE;
    uint8_t o = 0;
    while ((1ull << o) < pages) {
        if (o >= KM_ORDER_LIMIT)
            return 0;
        o++;
    }
    *order = o;
    return 1;
}

static int meta_index(void *ptr, uint64_t *pfn) {
    uint64_t address = (uint64_t)ptr;
    if (address < hhdm_offset)
        return 0;
    uint64_t index = (address - hhdm_offset) / KM_PAGE_SIZE;
    if (index >= meta_pages)
        return 0;
    *pfn = index;
    return 1;
}

int kmalloc_init(void) {
    if (km_ready)
        return 0;
    uint32_t count = memory_region_count();
    uint64_t max_pfn = 0;
    for (uint32_t i = 0; i < count; i++) {
        const struct mem_region *region = memory_region_get(i);
        if (!region)
            return 0;
        uint64_t end_pfn = region->end / KM_PAGE_SIZE;
        if (end_pfn > max_pfn)
            max_pfn = end_pfn;
    }
    if (max_pfn == 0)
        return 0;
    uint8_t order = 0;
    if (!order_for(max_pfn, &order))
        return 0;
    uint64_t phys = buddy_alloc_order(order);
    if (phys == 0)
        return 0;
    page_meta = (uint8_t *)phys_to_virt(phys);
    meta_pages = max_pfn;
    for (uint32_t i = 0; i < KM_CLASS_COUNT; i++) {
        if (!slub_cache_init(&caches[i], class_sizes[i], class_sizes[i], class_orders[i]))
            return 0;
    }
    km_ready = 1;
    return 1;
}

void *kmalloc(size_t size) {
    if (!km_ready || size == 0)
        return NULL;
    if (size <= KM_MAX_SMALL) {
        int cls = class_for(size);
        if (cls < 0)
            return NULL;
        void *object = slub_alloc(&caches[cls]);
        if (object == NULL)
            return NULL;
        uint64_t pfn;
        if (!meta_index(object, &pfn)) {
            slub_free(&caches[cls], object);
            return NULL;
        }
        page_meta[pfn] = (uint8_t)(cls + 1);
        return object;
    }
    uint8_t order;
    if (!order_for(size, &order))
        return NULL;
    uint64_t phys = buddy_alloc_order(order);
    if (phys == 0)
        return NULL;
    void *block = phys_to_virt(phys);
    uint64_t pfn;
    if (!meta_index(block, &pfn)) {
        buddy_free_order(phys, order);
        return NULL;
    }
    page_meta[pfn] = (uint8_t)(KM_LARGE_FLAG | order);
    return block;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr == NULL)
        return NULL;
    size_t usable = ksize(ptr);
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0; i < usable; i++)
        bytes[i] = 0;
    return ptr;
}

size_t ksize(void *ptr) {
    uint64_t pfn;
    if (!km_ready || ptr == NULL || !meta_index(ptr, &pfn))
        return 0;
    uint8_t meta = page_meta[pfn];
    if (meta & KM_LARGE_FLAG)
        return (size_t)(KM_PAGE_SIZE << (meta & KM_ORDER_MASK));
    if (meta >= 1 && meta <= KM_CLASS_COUNT)
        return class_sizes[meta - 1];
    return 0;
}

void kfree(void *ptr) {
    uint64_t pfn;
    if (!km_ready || ptr == NULL || !meta_index(ptr, &pfn))
        return;
    uint8_t meta = page_meta[pfn];
    if (meta & KM_LARGE_FLAG) {
        page_meta[pfn] = 0;
        buddy_free_order((uint64_t)ptr - hhdm_offset, (uint8_t)(meta & KM_ORDER_MASK));
        return;
    }
    if (meta >= 1 && meta <= KM_CLASS_COUNT)
        slub_free(&caches[meta - 1], ptr);
}

void *krealloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    size_t old_size = ksize(ptr);
    if (old_size == 0)
        return NULL;
    if (size <= old_size)
        return ptr;
    void *fresh = kmalloc(size);
    if (fresh == NULL)
        return NULL;
    uint8_t *dst = (uint8_t *)fresh;
    uint8_t *src = (uint8_t *)ptr;
    for (size_t i = 0; i < old_size; i++)
        dst[i] = src[i];
    kfree(ptr);
    return fresh;
}

