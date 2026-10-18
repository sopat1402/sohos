#ifndef SOHOS_NX_TEST_H
#define SOHOS_NX_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"
#include "../include/virtual_manager.h"

#define NX_TEST_PAGE_SIZE 4096ULL
#define NX_TEST_PAGE_MASK (~(NX_TEST_PAGE_SIZE - 1ULL))
#define NX_TEST_FLAG_NX (1ULL << 63)
#define NX_TEST_MSR_EFER 0xC0000080U
#define NX_TEST_EFER_NXE (1ULL << 11)
#define NX_TEST_CPUID_EXTENDED 0x80000000U
#define NX_TEST_CPUID_NX_LEAF 0x80000001U
#define NX_TEST_CPUID_NX (1U << 20)

extern int vm_set_flags_local(uint64_t pml4_phys, uint64_t virt_addr, uint64_t set_bits, uint64_t clear_bits);

static __attribute__((noinline, used, aligned(4096), section(".text.nx_test_probe"))) void nx_test_probe(void) {
    __asm__ volatile("" ::: "memory");
}

static inline int nx_test_run(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = NX_TEST_CPUID_EXTENDED;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < NX_TEST_CPUID_NX_LEAF) { print("NX: no extended CPUID\n"); return 0; }

    eax = NX_TEST_CPUID_NX_LEAF;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & NX_TEST_CPUID_NX)) { print("NX: unsupported\n"); return 0; }

    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(NX_TEST_MSR_EFER));
    if (!((((uint64_t)hi << 32) | lo) & NX_TEST_EFER_NXE)) { print("NX: NXE disabled\n"); return 0; }

    uint64_t cr3, phys, perms;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xfffULL;

    uintptr_t addr = (uintptr_t)nx_test_probe;
    uint64_t page = (uint64_t)addr & NX_TEST_PAGE_MASK;
    if (addr != page) { print("NX: bad alignment\n"); return 0; }

    enum vm_walk_status s = vm_translate(cr3, page, &phys, &perms);
    if (s != VM_WALK_OK || (perms & VM_LARGE) || !(perms & VM_EXEC) || (perms & VM_WRITE)) {
        print("NX: invalid initial mapping\n");
        return 0;
    }

    if (!vm_set_flags_local(cr3, page, NX_TEST_FLAG_NX, 0)) {
        print("NX: set failed\n");
        return 0;
    }

    s = vm_translate(cr3, page, &phys, &perms);
    int passed = s == VM_WALK_OK && !(perms & (VM_LARGE | VM_EXEC | VM_WRITE));

    if (!vm_set_flags_local(cr3, page, 0, NX_TEST_FLAG_NX)) {
        print("NX: restore failed; halted\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }

    s = vm_translate(cr3, page, &phys, &perms);
    if (s != VM_WALK_OK || (perms & VM_LARGE) || !(perms & VM_EXEC) || (perms & VM_WRITE))
        passed = 0;

    print(passed ? "NX: PASS\n" : "NX: FAIL\n");
    return passed;
}
#endif
