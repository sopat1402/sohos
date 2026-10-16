#include <stdint.h>
#include "../include/thread.h"
#include "../include/sched.h"
#include "../include/percpu.h"
#include "../include/kmalloc.h"
#include "../include/buddy.h"
#include "../include/phys.h"
#include "../include/spinlock.h"

#define THREAD_FRAME_BYTES 72u

static uint32_t next_thread_id = 1;

uint32_t thread_alloc_id(void) {
    return __atomic_fetch_add(&next_thread_id, 1, __ATOMIC_RELAXED);
}

void thread_set_name(struct thread *t, const char *name) {
    uint32_t i = 0;
    if (name != 0) {
        for (; i < THREAD_NAME_LEN - 1 && name[i] != '\0'; i++)
            t->name[i] = name[i];
    }
    t->name[i] = '\0';
}

struct thread *thread_current(void) {
    return (struct thread *)percpu_self()->current_thread;
}

void thread_free(struct thread *t) {
    if (t == 0)
        return;
    if (t->stack_phys != 0)
        buddy_free_order(t->stack_phys, THREAD_STACK_ORDER);
    kfree(t);
}

struct thread *thread_create_on(const char *name, thread_fn fn, void *arg, uint32_t cpu) {
    if (fn == 0)
        return 0;
    struct thread *t = (struct thread *)kzalloc(sizeof(struct thread));
    if (t == 0)
        return 0;
    uint64_t phys = buddy_alloc_order(THREAD_STACK_ORDER);
    if (phys == 0) {
        kfree(t);
        return 0;
    }
    uint8_t *base = (uint8_t *)phys_to_virt(phys);
    uint64_t size = 4096ull << THREAD_STACK_ORDER;
    *(uint64_t *)base = THREAD_CANARY;
    uint64_t top = (uint64_t)(uintptr_t)base + size;
    uint64_t *frame = (uint64_t *)(top - THREAD_FRAME_BYTES);
    frame[0] = 0;
    frame[1] = 0;
    frame[2] = (uint64_t)(uintptr_t)arg;
    frame[3] = (uint64_t)(uintptr_t)fn;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = (uint64_t)(uintptr_t)thread_trampoline;
    frame[7] = 0;
    frame[8] = 0;
    t->saved_rsp = (uint64_t)(uintptr_t)frame;
    t->stack_phys = phys;
    t->stack_base = base;
    t->stack_size = size;
    t->id = thread_alloc_id();
    t->state = THREAD_NEW;
    thread_set_name(t, name);
    if (!sched_enqueue(t, cpu)) {
        thread_free(t);
        return 0;
    }
    return t;
}

struct thread *thread_create(const char *name, thread_fn fn, void *arg) {
    uint32_t cpu = sched_pick_cpu();
    if (cpu == SCHED_NO_CPU)
        return 0;
    return thread_create_on(name, fn, arg, cpu);
}

__attribute__((noreturn)) void thread_exit(void) {
    (void)irq_save();
    struct thread *t = thread_current();
    t->state = THREAD_ZOMBIE;
    schedule();
    for (;;)
        __asm__ volatile ("cli\n\thlt");
}

void set_tss_rsp0_currthrd(){
    struct thread *curr_thrd=thread_current();
    struct percpu *p=percpu_self();
    p->tss.rsp[0]=(uint64_t)(uintptr_t)phys_to_virt(curr_thrd->stack_phys + curr_thrd->stack_size) & ~0xFull;
}
