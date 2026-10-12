#ifndef SLUB
#define SLUB

#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"

typedef struct slab_t slab_t;

typedef struct slub_cache_t {
    size_t object_size;
    size_t alignment;
    size_t slot_size;
    size_t obj_per_slab;
    uint8_t slab_order;
    spinlock_t lock;
    slab_t *slab_list;
} slub_cache_t;

struct slab_t {
    uint64_t *pages;
    uint64_t phys_base;
    struct free_slot* free_objs;
    size_t total_objects;
    size_t free_count;
    size_t slot_size;
    slab_t *next;
};

typedef struct free_slot {
    struct free_slot *next;
} free_slot_t;

int slab_init(slab_t *slab, slub_cache_t *cache, void *mapped_base, size_t slab_bytes);
void *slab_alloc(slab_t *slab);
int slab_free(slab_t *slab, void* object);
int slub_cache_init(slub_cache_t *cache, size_t object_size, size_t alignment, uint8_t slab_order);
void *slub_alloc(slub_cache_t *cache);
int slub_free(slub_cache_t *cache, void *object);
#endif
