#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>

#define IA32_GS_BASE_MSR 0xC0000101u

struct percpu {
    struct percpu *self;
    uint32_t cpu_id;
    uint32_t apic_id;
    uint64_t ticks;
    void *current_thread;
} __attribute__((aligned(64)));

int percpu_init(uint32_t bsp_apic_id, const uint32_t *ap_apic_ids, uint32_t ap_count);
int percpu_attach(uint32_t apic_id);
struct percpu *percpu_by_apic_id(uint32_t apic_id);
struct percpu *percpu_by_cpu_id(uint32_t cpu_id);
uint32_t percpu_count(void);

static inline struct percpu *percpu_self(void) {
    struct percpu *p;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(p));
    return p;
}

#endif
