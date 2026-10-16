#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>

#define IA32_GS_BASE_MSR 0xC0000101u

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss) == 104, "Invalid TSS size");

struct percpu {
    struct percpu *self;
    uint32_t cpu_id;
    uint32_t apic_id;
    uint64_t ticks;
    void *current_thread;
    uint32_t tlb_online;
    uint64_t tlb_ack_generation;
    void *boot_stack_base;
    uint64_t boot_stack_size;
    uint64_t gdt[7] __attribute__((aligned(64)));
    struct tss tss __attribute__((aligned(64)));
} __attribute__((aligned(64)));

int percpu_init(uint32_t bsp_apic_id, const uint32_t *ap_apic_ids, uint32_t ap_count);
int percpu_attach(uint32_t apic_id);
struct percpu *percpu_by_apic_id(uint32_t apic_id);
struct percpu *percpu_by_cpu_id(uint32_t cpu_id);
uint32_t percpu_count(void);
int setup_ist(struct percpu *p);
static inline struct percpu *percpu_self(void) {
    struct percpu *p;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(p));
    return p;
}

#endif
