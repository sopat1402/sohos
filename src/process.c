#include <stdint.h>
#include <stddef.h>
#include "../include/address_space.h"
#include "../include/virtual_manager.h"
#include "../include/wait_queue.h"
#include "../include/sched.h"
#include "../include/kmalloc.h"
#include "../include/thread.h"
#include "../include/phys.h"
#include "../include/elf_loader.h"
#include "../include/process.h"
#include "../include/kstring.h"
#include "../include/initrd.h"

#define USER_STACK_TOP   0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 4ULL
#define USER_PAGE_SIZE   4096ULL
#define USER_CS     0x23ULL
#define USER_SS     0x1BULL
#define USER_RFLAGS 0x202ULL

extern uint8_t user_exit_start[];
extern uint8_t user_exit_end[];

static uint32_t next_pid=1;
static spinlock_t proc_lock=SPINLOCK_INIT;
#define MAX_PROCESSES 1024

struct user_launch {
    struct process *proc;
    uint64_t entry;
    uint64_t stack_top;
};

static struct process **proc_table = NULL;
static size_t process_table_capacity = 0;

int add_to_table(struct process *proc) {
    if (proc_table == NULL) {
        process_table_capacity = 64;
        proc_table = (struct process **)kmalloc(process_table_capacity * sizeof(struct process *));
        if (proc_table == NULL)
            return 0;
        for (size_t i = 0; i < process_table_capacity; i++) {
            proc_table[i] = NULL;
        }
    }
    while (proc->pid >= process_table_capacity) {
        size_t new_cap = process_table_capacity * 2;
        struct process **new_table = (struct process **)krealloc(proc_table, new_cap * sizeof(struct process *));
        if (new_table == NULL)
            return 0;
        for (size_t i = process_table_capacity; i < new_cap; i++) {
            new_table[i] = NULL;
        }
        proc_table = new_table;
        process_table_capacity = new_cap;
    }
    proc_table[proc->pid] = proc;
    return 1;
}

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
    t->proc = launch.proc;
    t->as = launch.proc->as;
    launch.proc->main_thread = t;
    as_switch(launch.proc->as);

    jump_to_user(launch.entry, launch.stack_top);
}

static void child_list_unlink(struct process *parent, struct process *child) {
    struct process **link = &parent->children;

    while (*link != NULL) {
        if (*link == child) {
            *link = child->sibling;
            child->sibling = NULL;
            return;
        }
        link = &(*link)->sibling;
    }
}

struct process *process_create(const char *name, const void *elf_data, uint64_t elf_size) {
    struct process *parent = current_process();
    struct address_space *as = as_create();

    if (as == NULL)
        return NULL;

    uint64_t entry = 0;

    if (elf_load(elf_data, elf_size, as, &entry) != 0 || map_user_stack(as) != 0) {
        as_destroy(as);
        return NULL;
    }

    struct process *self = (struct process *)kzalloc(sizeof(struct process));
    struct user_launch *launch = (struct user_launch *)kmalloc(sizeof(*launch));

    if (self == NULL || launch == NULL) {
        if (self != NULL)
            kfree(self);
        if (launch != NULL)
            kfree(launch);
        as_destroy(as);
        return NULL;
    }

    self->state = PROCESS_RUNNING;
    self->as = as;
    self->parent = parent;
    wait_queue_init(&self->child_wq);

    launch->proc = self;
    launch->proc->as = as;
    launch->entry = entry;
    launch->stack_top = USER_STACK_TOP;

    uint64_t lock_flags = spin_lock_irqsave(&proc_lock);
    self->pid = next_pid;

    if (!add_to_table(self)) {
        spin_unlock_irqrestore(&proc_lock, lock_flags);
        kfree(self);
        kfree(launch);
        as_destroy(as);
        return NULL;
    }

    next_pid++;

    if (parent != NULL) {
        self->sibling = parent->children;
        parent->children = self;
    }

    spin_unlock_irqrestore(&proc_lock, lock_flags);

    struct thread *t = thread_create(name, user_trampoline, launch);

    if (t == NULL) {
        lock_flags = spin_lock_irqsave(&proc_lock);
        proc_table[self->pid] = NULL;

        if (parent != NULL)
            child_list_unlink(parent, self);

        spin_unlock_irqrestore(&proc_lock, lock_flags);
        kfree(self);
        kfree(launch);
        as_destroy(as);
        return NULL;
    }

    return self;
}

struct process *process_get_by_pid(uint32_t pid) {
    uint64_t lock_flags = spin_lock_irqsave(&proc_lock);
    if (pid >= process_table_capacity || proc_table == NULL) {
        spin_unlock_irqrestore(&proc_lock, lock_flags);
        return NULL;
    }
    struct process *proc = proc_table[pid];
    spin_unlock_irqrestore(&proc_lock, lock_flags);
    return proc;
}

struct process *current_process(void){
    struct thread *curr_thread=thread_current();
    return curr_thread->proc;
}

void sys_exit(int code) {
    struct process *proc = current_process();
    if (!proc) {
        thread_exit();
        return;
    }
    uint64_t lock_flags = spin_lock_irqsave(&proc_lock);
    proc->exit_code = code;
    proc->state = PROCESS_ZOMBIE;
    struct process *child = proc->children;
    while (child != NULL) {
        child->parent = NULL;
        child = child->sibling;
    }
    proc->children = NULL;
    if (proc->parent) {
        wait_queue_wake_one(&proc->parent->child_wq);
    }
    spin_unlock_irqrestore(&proc_lock, lock_flags);
    thread_exit();
}

int32_t process_wait(int32_t pid, int *status_out) {
    struct process *self = current_process();
    if (!self) {
        return -1;
    }
    uint64_t lock_flags = spin_lock_irqsave(&proc_lock);
    while (1) {
        struct process *curr = self->children;
        struct process *prev = NULL;
        int found_child_exists = 0;
        while (curr != NULL) {
            if (pid == -1 || curr->pid == (uint32_t)pid) {
                found_child_exists = 1;
                if (curr->state == PROCESS_ZOMBIE) {
                    int32_t reaped_pid = (int32_t)curr->pid;
                    if (status_out != NULL) {
                        *status_out = curr->exit_code;
                    }
                    if (prev == NULL) {
                        self->children = curr->sibling;
                    } else {
                        prev->sibling = curr->sibling;
                    }
                    if (reaped_pid >= 0 && (size_t)reaped_pid < process_table_capacity) {
                        proc_table[reaped_pid] = NULL;
                    }
                    spin_unlock_irqrestore(&proc_lock, lock_flags);
                    kfree(curr);
                    return reaped_pid;
                }
            }
            prev = curr;
            curr = curr->sibling;
        }
        if (!found_child_exists) {
            spin_unlock_irqrestore(&proc_lock, lock_flags);
            return -1;
        }
        struct wait_queue_entry wq_entry;
        wait_queue_prepare_to_wait(&self->child_wq, &wq_entry, &lock_flags);
        spin_unlock_irqrestore(&proc_lock, lock_flags);
        schedule();
        lock_flags = spin_lock_irqsave(&proc_lock);
    }
}

int32_t sys_spawn(const char *user_path) {
    char path_buf[128];
    if (strncpy_from_user(path_buf, user_path, sizeof(path_buf)) < 0)
        return -1;
    const struct initrd_file *file = initrd_lookup(path_buf);
    if (file == NULL)
        return -1;
    struct process *child = process_create(path_buf, file->data, file->size);
    if (child == NULL)
        return -1;
    return (int32_t)child->pid;
}
