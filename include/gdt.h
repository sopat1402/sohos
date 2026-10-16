#ifndef GDT_H
#define GDT_H

#include "percpu.h"

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void gdt_load(void);
void percpu_gdt_setup(struct percpu *p);

#endif
