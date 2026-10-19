#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include <stdint.h>
#include "spinlock.h"

struct address_space {
    uint64_t pml4_phys;
    uint32_t refcount;
    uint32_t pad;
    uint64_t cpu_mask;
    spinlock_t lock;
};

extern struct address_space kernel_as;

int kernel_as_init(void);
struct address_space *as_create(void);
struct address_space *as_get(struct address_space *as);
struct address_space *as_create(void);
int as_destroy(struct address_space *as);
void as_put(struct address_space *as);

#endif
