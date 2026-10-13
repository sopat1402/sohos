#include <stdint.h>
#include "../include/slub.h"
#include "../include/buddy.h"
#include "../include/phys.h"
#include "../include/spinlock.h"

#define PAGE_SIZE 4096u
#define BUDDY_MAX_ORDER 47u

static uint64_t align_up(uint64_t x, uint64_t a){
    return (x+a-1)&~(a-1);
}

int slab_init(slab_t *slab, slub_cache_t *cache, void *mapped_base, size_t slab_bytes) {
    if (slab == NULL ||
        cache == NULL ||
        mapped_base == NULL ||
        slab_bytes == 0 ||
        cache->object_size == 0 ||
        cache->alignment == 0 ||
        (cache->alignment & (cache->alignment - 1)) != 0)
        return 0;
    size_t min_slot_size = sizeof(struct free_slot);
    if (cache->object_size > min_slot_size)
        min_slot_size = cache->object_size;
    size_t alignment = cache->alignment;
    if (alignment < _Alignof(struct free_slot))
        alignment = _Alignof(struct free_slot);
    size_t stride = align_up(min_slot_size, alignment);
    if (stride == 0 || stride > slab_bytes)
        return 0;
    uint8_t *base = (uint8_t *)mapped_base;
    size_t count = slab_bytes / stride;
    struct free_slot *head = NULL;
    for (size_t i = count; i > 0; i--) {
        struct free_slot *slot = (struct free_slot *)(base + (i - 1) * stride);
        slot->next = head;
        head = slot;
    }
    slab->pages = (uint64_t *)mapped_base;
    slab->free_objs = head;
    slab->total_objects = count;
    slab->free_count = count;
    slab->slot_size = stride;
    slab->next = NULL;
    cache->slot_size = stride;
    cache->obj_per_slab = count;
    return 1;
}

void* slab_alloc(slab_t *slab){
    if (slab==NULL || slab->free_objs==NULL || slab->free_count==0){
        return NULL;
    }
    free_slot_t *popped=slab->free_objs;
    slab->free_objs=(void*)popped->next;
    slab->free_count--;
    return popped;
}

int slab_free(slab_t *slab, void *object) {
    if (slab == NULL || object == NULL || slab->pages == NULL || slab->slot_size == 0)
        return 0;
    uintptr_t base = (uintptr_t)slab->pages;
    uintptr_t address = (uintptr_t)object;
    if (address < base)
        return 0;
    uintptr_t offset = address - base;
    if (offset / slab->slot_size >= slab->total_objects || offset % slab->slot_size != 0)
        return 0;
    for (free_slot_t *node = slab->free_objs; node != NULL; node = node->next) {
        if ((void *)node == object)
            return 0;
    }
    free_slot_t *slot = (free_slot_t *)object;
    slot->next = slab->free_objs;
    slab->free_objs = slot;
    slab->free_count++;
    return 1;
}

static int slab_contains(const slab_t *slab, const void *object) {
    uintptr_t lo = (uintptr_t)slab->pages;
    uintptr_t hi = lo + slab->total_objects * slab->slot_size;
    uintptr_t address = (uintptr_t)object;
    return address >= lo && address < hi;
}

int slub_cache_init(slub_cache_t *cache, size_t object_size, size_t alignment, uint8_t slab_order) {
    if (cache == NULL || object_size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
        return 0;
    if (slab_order > BUDDY_MAX_ORDER)
        return 0;
    cache->object_size = object_size;
    cache->alignment = alignment;
    cache->slot_size = 0;
    cache->obj_per_slab = 0;
    cache->slab_order = slab_order;
    atomic_flag_clear(&cache->lock.flag);
    cache->slab_list = NULL;
    return 1;
}

static slab_t *slub_grow(slub_cache_t *cache) {
    uint64_t phys = buddy_alloc_order(cache->slab_order);
    if (phys == 0)
        return NULL;
    size_t block_bytes = (size_t)PAGE_SIZE << cache->slab_order;
    uint8_t *block = (uint8_t *)phys_to_virt(phys);
    slab_t *slab = (slab_t *)block;
    size_t alignment = cache->alignment;
    if (alignment < _Alignof(struct free_slot))
        alignment = _Alignof(struct free_slot);
    size_t header = (size_t)align_up(sizeof(slab_t), alignment);
    if (header >= block_bytes || !slab_init(slab, cache, block + header, block_bytes - header)) {
        buddy_free_order(phys, cache->slab_order);
        return NULL;
    }
    slab->phys_base = phys;
    return slab;
}

void *slub_alloc(slub_cache_t *cache) {
    if (cache == NULL)
        return NULL;
    uint64_t flags = spin_lock_irqsave(&cache->lock);
    for (slab_t *slab = cache->slab_list; slab != NULL; slab = slab->next) {
        if (slab->free_count == 0)
            continue;
        void *object = slab_alloc(slab);
        spin_unlock_irqrestore(&cache->lock, flags);
        return object;
    }
    slab_t *fresh = slub_grow(cache);
    if (fresh == NULL) {
        spin_unlock_irqrestore(&cache->lock, flags);
        return NULL;
    }
    fresh->next = cache->slab_list;
    cache->slab_list = fresh;
    void *object = slab_alloc(fresh);
    spin_unlock_irqrestore(&cache->lock, flags);
    return object;
}

int slub_free(slub_cache_t *cache, void *object) {
    if (cache == NULL || object == NULL)
        return 0;
    uint64_t flags = spin_lock_irqsave(&cache->lock);
    slab_t *prev = NULL;
    slab_t *slab = cache->slab_list;
    while (slab != NULL && !slab_contains(slab, object)) {
        prev = slab;
        slab = slab->next;
    }
    if (slab == NULL || !slab_free(slab, object)) {
        spin_unlock_irqrestore(&cache->lock, flags);
        return 0;
    }
    int release = slab->free_count == slab->total_objects && (prev != NULL || slab->next != NULL);
    uint64_t phys = slab->phys_base;
    if (release) {
        if (prev != NULL)
            prev->next = slab->next;
        else
            cache->slab_list = slab->next;
    }
    spin_unlock_irqrestore(&cache->lock, flags);
    if (release)
        buddy_free_order(phys, cache->slab_order);
    return 1;
}

