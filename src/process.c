#include <stdint.h>
#include <stddef.h>
#include "../include/address_space.h"
#include "../include/virtual_manager.h"
#include "../include/sched.h"
#include "../include/kmalloc.h"
#include "../include/thread.h"
#include "../include/phys.h"
#include "../include/elf_loader.h"
#include "../include/process.h"

#define USER_STACK_TOP   0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 4ULL
#define USER_PAGE_SIZE   4096ULL

#define USER_CS     0x23ULL
#define USER_SS     0x1BULL
#define USER_RFLAGS 0x202ULL

struct user_launch {
    struct address_space *as;
    uint64_t entry;
    uint64_t stack_top;
};

static int map_user_stack(struct address_space *as) {
    for (uint64_t i = 0; i < USER_STACK_PAGES; i++) {
        uint64_t vaddr = USER_STACK_TOP - (i + 1) * USER_PAGE_SIZE;
        uint64_t phys = 0;

        if (vm_map(as->pml4_phys, vaddr, VM_READ | VM_WRITE | VM_USER, &phys) != VM_MAP_OK || phys == 0)
            return -1;

        __builtin_memset(phys_to_virt(phys), 0, USER_PAGE_SIZE);
    }

    return 0;
}

static __attribute__((noreturn)) void jump_to_user(uint64_t entry, uint64_t stack_top) {
    __asm__ volatile (
        "swapgs\n\t"
        "pushq %[ss]\n\t"
        "pushq %[sp]\n\t"
        "pushq %[fl]\n\t"
        "pushq %[cs]\n\t"
        "pushq %[ip]\n\t"
        "xorl %%eax, %%eax\n\t"
        "xorl %%ebx, %%ebx\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "xorl %%edx, %%edx\n\t"
        "xorl %%esi, %%esi\n\t"
        "xorl %%edi, %%edi\n\t"
        "xorl %%ebp, %%ebp\n\t"
        "xorl %%r8d, %%r8d\n\t"
        "xorl %%r9d, %%r9d\n\t"
        "xorl %%r10d, %%r10d\n\t"
        "xorl %%r11d, %%r11d\n\t"
        "xorl %%r12d, %%r12d\n\t"
        "xorl %%r13d, %%r13d\n\t"
        "xorl %%r14d, %%r14d\n\t"
        "xorl %%r15d, %%r15d\n\t"
        "iretq"
        :
        : [ss] "r"((uint64_t)USER_SS), [sp] "r"(stack_top), [fl] "r"((uint64_t)USER_RFLAGS),
          [cs] "r"((uint64_t)USER_CS), [ip] "r"(entry)
        : "memory");

    __builtin_unreachable();
}

static void user_trampoline(void *arg) {
    struct user_launch launch = *(struct user_launch *)arg;
    kfree(arg);
    __asm__ volatile ("cli" ::: "memory");
    struct thread *t = thread_current();
    t->as = launch.as;
    as_switch(launch.as);
    jump_to_user(launch.entry, launch.stack_top);
}

struct thread *process_create(const char *name, const void *elf_data, uint64_t elf_size) {
    struct address_space *as = as_create();
    if (as == NULL)
        return NULL;
    uint64_t entry = 0;
    if (elf_load(elf_data, elf_size, as, &entry) != 0 || map_user_stack(as) != 0) {
        as_destroy(as);
        return NULL;
    }
    struct user_launch *launch = (struct user_launch *)kmalloc(sizeof(*launch));
    if (launch == NULL) {
        as_destroy(as);
        return NULL;
    }
    launch->as = as;
    launch->entry = entry;
    launch->stack_top = USER_STACK_TOP;
    struct thread *t = thread_create(name, user_trampoline, launch);
    if (t == NULL) {
        kfree(launch);
        as_destroy(as);
        return NULL;
    }
    return t;
}
