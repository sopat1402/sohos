#ifndef SOHOS_GUARD_STACK_TEST_H
#define SOHOS_GUARD_STACK_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"
#include "../include/virtual_manager.h"
#include "../include/vmalloc.h"

#define GUARD_TEST_PAGE_SIZE 4096ULL

static inline int guard_stack_test_run(void) {
    print("GuardStack: entered test\n");
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xfffULL;
    uint64_t alloc_size = GUARD_TEST_PAGE_SIZE * 2ULL;
    uint64_t addr = vmalloc(alloc_size);
    if (addr == 0) {
        print("GuardStack: vmalloc failed\n");
        return 0;
    }
    uint64_t phys = 0;
    uint64_t perms = 0;

    enum vm_walk_status s_guard = vm_translate(cr3, addr, &phys, &perms);
    int guard_unmapped = (s_guard != VM_WALK_OK);

    enum vm_walk_status s_usable = vm_translate(cr3, addr + GUARD_TEST_PAGE_SIZE, &phys, &perms);
    int usable_mapped = (s_usable == VM_WALK_OK && (perms & VM_READ) && (perms & VM_WRITE));
    int passed = guard_unmapped && usable_mapped;

    if (vfree(addr) != 0) {
        print("GuardStack: vfree failed\n");
        passed = 0;
    }

    print(passed ? "GuardStack: PASS\n" : "GuardStack: FAIL\n");
    return passed;
}

#endif
