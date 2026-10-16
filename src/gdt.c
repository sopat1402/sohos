#include <stdint.h>
#include "../include/gdt.h"
#include "../include/percpu.h"

static uint64_t gdt[3] __attribute__((aligned(16))) = {
    0x0000000000000000ull,
    0x00AF9B000000FFFFull,
    0x00CF93000000FFFFull
};

uint64_t gdt_7[7] = {
    0x0000000000000000ull,
    0x00AF9B000000FFFFull,
    0x00CF93000000FFFFull,
    0x00CFF3000000FFFFull,
    0x00AFFB000000FFFFull,
    0,
    0
};

void gdt_load(void) {
    struct gdt_pointer ptr;
    ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    ptr.base = (uint64_t)(uintptr_t)gdt;
    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        : : "m"(ptr), "i"(GDT_KERNEL_CODE), "i"(GDT_KERNEL_DATA) : "rax", "memory");
}

void percpu_gdt_setup(struct percpu *p){
    p->gdt[0]=gdt_7[0];
    p->gdt[1]=gdt_7[1];
    p->gdt[2]=gdt_7[2];
    p->gdt[3]=gdt_7[3];
    p->gdt[4]=gdt_7[4];
    uint64_t base = (uint64_t)(uintptr_t)&p->tss;
    uint64_t limit = sizeof(p->tss) - 1;
    p->gdt[5] = (limit & 0xFFFF)
              | ((base & 0xFFFFFF) << 16)
              | (0x89ull << 40)
              | (((limit >> 16) & 0xF) << 48)
              | (((base >> 24) & 0xFF) << 56);
    p->gdt[6] = base >> 32;
    struct gdt_pointer gdt_p = {
        .base = (uint64_t)(uintptr_t)p->gdt,
        .limit = sizeof(p->gdt) - 1
    };
    p->tss.iomap_base = sizeof(p->tss);
    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        : : "m"(gdt_p), "i"(GDT_KERNEL_CODE), "i"(GDT_KERNEL_DATA) : "rax", "memory");
    uint16_t tss_selector = 0x28;
    __asm__ volatile ("ltr %0" : : "r"(tss_selector) : "memory");
}
