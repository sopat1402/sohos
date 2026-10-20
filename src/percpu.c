#include <stdint.h>
#include "../include/percpu.h"
#include "../include/kmalloc.h"
#include "../include/phys.h"
#include "../include/buddy.h"

static struct percpu *cpus;
static uint32_t cpu_total;

#define IA32_KERNEL_GS_BASE_MSR 0xC0000102u

static void percpu_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

int percpu_init(uint32_t bsp_apic_id, const uint32_t *ap_apic_ids, uint32_t ap_count) {
    if (cpus != 0 || (ap_count != 0 && ap_apic_ids == 0))
        return 0;
    uint32_t total = ap_count + 1;
    struct percpu *array = (struct percpu *)kzalloc((size_t)total * sizeof(struct percpu));
    if (array == 0)
        return 0;
    if (((uintptr_t)array & 63u) != 0) {
        kfree(array);
        return 0;
    }
    for (uint32_t i = 0; i < total; i++) {
        array[i].self = &array[i];
        array[i].cpu_id = i;
        array[i].apic_id = i == 0 ? bsp_apic_id : ap_apic_ids[i - 1];
    }
    cpu_total = total;
    __atomic_store_n(&cpus, array, __ATOMIC_RELEASE);
    return 1;
}

struct percpu *percpu_by_apic_id(uint32_t apic_id) {
    struct percpu *array = __atomic_load_n(&cpus, __ATOMIC_ACQUIRE);
    if (array == 0)
        return 0;
    for (uint32_t i = 0; i < cpu_total; i++) {
        if (array[i].apic_id == apic_id)
            return &array[i];
    }
    return 0;
}

struct percpu *percpu_by_cpu_id(uint32_t cpu_id) {
    struct percpu *array = __atomic_load_n(&cpus, __ATOMIC_ACQUIRE);
    if (array == 0 || cpu_id >= cpu_total)
        return 0;
    return &array[cpu_id];
}

uint32_t percpu_count(void) {
    return __atomic_load_n(&cpus, __ATOMIC_ACQUIRE) ? cpu_total : 0;
}

int percpu_attach(uint32_t apic_id) {
    struct percpu *p = percpu_by_apic_id(apic_id);
    if (p == 0)
        return 0;
    percpu_wrmsr(IA32_GS_BASE_MSR, (uint64_t)(uintptr_t)p);
    percpu_wrmsr(IA32_KERNEL_GS_BASE_MSR, (uint64_t)(uintptr_t)p);
    return 1;
}

int setup_ist(struct percpu *p){
    uint64_t stack_base_phys=buddy_alloc_order(2);
    if (stack_base_phys==0) return 0;
    uint64_t stack_top_virt=(uint64_t)(uintptr_t)phys_to_virt(stack_base_phys)+4096*4;
    p->tss.ist[0]=stack_top_virt;
    return 1;
}

#define CR0_WP (1ULL << 16)
#define CR0_NW (1ULL << 29)
#define CR0_CD (1ULL << 30)

void cpu_fix_cr0(void) {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(CR0_CD | CR0_NW);
    cr0 |= CR0_WP;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
}
