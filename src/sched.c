#include <stdint.h>
#include "../include/sched.h"
#include "../include/thread.h"
#include "../include/percpu.h"
#include "../include/lapic.h"
#include "../include/idt.h"
#include "../include/kmalloc.h"
#include "../include/spinlock.h"
#include "../include/display.h"
#include "../include/sleep.h"
#include "../include/address_space.h"

#define SCHED_IPI_COMMAND (0x4000u | SCHED_IPI_VECTOR)

struct runqueue {
    spinlock_t lock;
    struct thread *head;
    struct thread *tail;
    struct thread *idle;
    uint32_t nr_ready;
    uint32_t online;
    uint32_t busy;
} __attribute__((aligned(64)));

static struct runqueue *rqs;
static uint32_t cpu_count;
static uint32_t place_cursor;

__attribute__((noreturn)) static void sched_panic(const char *message) {
    __asm__ volatile ("cli");
    print("\nSCHED PANIC: ");
    print(message);
    for (;;)
        __asm__ volatile ("hlt");
}

static void rq_push(struct runqueue *rq, struct thread *t) {
    t->next = 0;
    t->prev = rq->tail;
    if (rq->tail)
        rq->tail->next = t;
    else
        rq->head = t;
    rq->tail = t;
    __atomic_fetch_add(&rq->nr_ready, 1, __ATOMIC_RELAXED);
}

static struct thread *rq_pop(struct runqueue *rq) {
    struct thread *t = rq->head;
    if (t == 0)
        return 0;
    rq->head = t->next;
    if (rq->head)
        rq->head->prev = 0;
    else
        rq->tail = 0;
    t->next = 0;
    t->prev = 0;
    __atomic_fetch_sub(&rq->nr_ready, 1, __ATOMIC_RELAXED);
    return t;
}

static void sched_ipi_handler(struct interrupt_frame *frame) {
    (void)frame;
    lapic_eoi();
    struct percpu *p = percpu_self();
    struct runqueue *rq = &rqs[p->cpu_id];
    if (p->current_thread == rq->idle && __atomic_load_n(&rq->nr_ready, __ATOMIC_RELAXED) > 0)
        schedule();
}

int sched_init(void) {
    if (rqs != 0)
        return 0;
    uint32_t n = percpu_count();
    if (n == 0)
        return 0;
    struct runqueue *array = (struct runqueue *)kzalloc((size_t)n * sizeof(struct runqueue));
    if (array == 0)
        return 0;
    if (((uintptr_t)array & 63u) != 0) {
        kfree(array);
        return 0;
    }
    for (uint32_t i = 0; i < n; i++)
        atomic_flag_clear(&array[i].lock.flag);
    if (!irq_register(SCHED_IPI_VECTOR, sched_ipi_handler)) {
        kfree(array);
        return 0;
    }
    cpu_count = n;
    __atomic_store_n(&rqs, array, __ATOMIC_RELEASE);
    lapic_set_tick_hook(sched_tick);
    return 1;
}

int sched_adopt_boot_thread(void) {
    if (rqs == 0)
        return 0;

    uint64_t flags = irq_save();
    struct percpu *p = percpu_self();

    if (p->boot_stack_base == 0 || p->boot_stack_size < sizeof(uint64_t)) {
        irq_restore(flags);
        return 0;
    }

    struct runqueue *rq = &rqs[p->cpu_id];
    struct thread *t = (struct thread *)kzalloc(sizeof(struct thread));
    if (t == 0) {
        irq_restore(flags);
        return 0;
    }

    t->id = thread_alloc_id();
    t->state = THREAD_RUNNING;
    t->cpu = p->cpu_id;
    t->on_cpu = 1;
    t->stack_base = p->boot_stack_base;
    t->stack_size = p->boot_stack_size;
    *(uint64_t *)t->stack_base = THREAD_CANARY;

    thread_set_name(t, "idle");
    rq->idle = t;
    p->current_thread = t;
    t->as = &kernel_as;
    t->proc=NULL;
    p->curr_as = &kernel_as;
    __atomic_fetch_or(&kernel_as.cpu_mask, 1ull << p->cpu_id, __ATOMIC_SEQ_CST);
    __atomic_store_n(&rq->online, 1, __ATOMIC_RELEASE);

    irq_restore(flags);
    return 1;
}

void as_switch(struct address_space *next) {
    struct percpu *p = percpu_self();
    struct address_space *prev = p->curr_as;

    if (next == NULL)
        next = &kernel_as;
    if (prev == next)
        return;

    uint64_t bit = 1ull << p->cpu_id;

    __atomic_fetch_or(&next->cpu_mask, bit, __ATOMIC_SEQ_CST);
    __asm__ volatile ("mov %0, %%cr3" :: "r"(next->pml4_phys) : "memory");
    p->curr_as = next;

    if (prev != NULL)
        __atomic_fetch_and(&prev->cpu_mask, ~bit, __ATOMIC_RELEASE);
}

void sched_finish_switch(struct thread *prev) {
    struct percpu *p = percpu_self();
    as_switch(((struct thread*)p->current_thread)->as);

    if (prev == 0)
        return;

    struct runqueue *rq = &rqs[prev->cpu];
    uint64_t flags = spin_lock_irqsave(&rq->lock);

    __atomic_store_n(&prev->on_cpu, 0, __ATOMIC_RELEASE);

    if (prev->wake_pending) {
        prev->wake_pending = 0;

        if (prev->state == THREAD_BLOCKED) {
            prev->state = THREAD_READY;
            prev->slice_left = SCHED_SLICE_TICKS;
            rq_push(rq, prev);
        }
    }

    int zombie = prev->state == THREAD_ZOMBIE;

    spin_unlock_irqrestore(&rq->lock, flags);

    if (zombie)
        thread_free(prev);
}

void schedule(void) {
    uint64_t flags = irq_save();
    struct percpu *p = percpu_self();
    struct runqueue *rq = &rqs[p->cpu_id];
    struct thread *prev = (struct thread *)p->current_thread;
    if (prev == 0) {
        irq_restore(flags);
        return;
    }
    spin_lock(&rq->lock);
    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        if (prev != rq->idle)
            rq_push(rq, prev);
    }
    struct thread *next = rq_pop(rq);
    if (next == 0)
        next = rq->idle;
    next->state = THREAD_RUNNING;
    next->slice_left = SCHED_SLICE_TICKS;
    if (next == prev) {
        spin_unlock(&rq->lock);
        irq_restore(flags);
        return;
    }
    if (prev->stack_base != 0 && *(uint64_t *)prev->stack_base != THREAD_CANARY)
        sched_panic("kernel stack overflow");
    __atomic_store_n(&next->on_cpu, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&rq->busy, next != rq->idle ? 1u : 0u, __ATOMIC_RELAXED);
    p->current_thread = next;
    set_tss_rsp0_currthrd();
    spin_unlock(&rq->lock);
    struct thread *last = switch_to(prev, next);
    sched_finish_switch(last);
    irq_restore(flags);
}

void sched_yield(void) {
    schedule();
}

void sched_tick(void) {
    struct percpu *p = percpu_self();
    struct runqueue *rq = &rqs[p->cpu_id];
    struct thread *cur = (struct thread *)p->current_thread;
    if (cur == 0 || __atomic_load_n(&rq->online, __ATOMIC_ACQUIRE) == 0)
        return;
    sleep_ticks_tick();
    uint32_t ready = __atomic_load_n(&rq->nr_ready, __ATOMIC_RELAXED);
    if (cur == rq->idle) {
        if (ready > 0)
            schedule();
        return;
    }
    if (cur->slice_left > 0)
        cur->slice_left--;
    if (cur->slice_left == 0) {
        if (ready > 0)
            schedule();
        else
            cur->slice_left = SCHED_SLICE_TICKS;
    }
}

uint32_t sched_pick_cpu(void) {
    if (rqs == 0)
        return SCHED_NO_CPU;
    uint32_t start = __atomic_fetch_add(&place_cursor, 1, __ATOMIC_RELAXED) % cpu_count;
    uint32_t best = SCHED_NO_CPU;
    uint32_t best_load = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < cpu_count; i++) {
        uint32_t c = (start + i) % cpu_count;
        struct runqueue *rq = &rqs[c];
        if (__atomic_load_n(&rq->online, __ATOMIC_ACQUIRE) == 0)
            continue;
        uint32_t load = __atomic_load_n(&rq->nr_ready, __ATOMIC_RELAXED) + __atomic_load_n(&rq->busy, __ATOMIC_RELAXED);
        if (load < best_load) {
            best_load = load;
            best = c;
        }
    }
    return best;
}

int sched_enqueue(struct thread *t, uint32_t cpu) {
    if (t == 0 || rqs == 0 || cpu >= cpu_count)
        return 0;
    struct runqueue *rq = &rqs[cpu];
    if (__atomic_load_n(&rq->online, __ATOMIC_ACQUIRE) == 0)
        return 0;
    uint64_t flags = irq_save();
    uint32_t me = percpu_self()->cpu_id;
    spin_lock(&rq->lock);
    t->cpu = cpu;
    t->state = THREAD_READY;
    t->slice_left = SCHED_SLICE_TICKS;
    rq_push(rq, t);
    spin_unlock(&rq->lock);
    irq_restore(flags);
    if (cpu != me) {
        struct percpu *target = percpu_by_cpu_id(cpu);
        if (target != 0)
            lapic_send_ipi(target->apic_id, SCHED_IPI_COMMAND);
    }
    return 1;
}

__attribute__((noreturn)) void sched_idle_loop(void) {
    struct runqueue *rq = &rqs[percpu_self()->cpu_id];
    for (;;) {
        __asm__ volatile ("cli" : : : "memory");
        if (__atomic_load_n(&rq->nr_ready, __ATOMIC_ACQUIRE) > 0) {
            __asm__ volatile ("sti" : : : "memory"); // <--- Enable interrupts before scheduling!
            schedule();
            continue;
        }
        __asm__ volatile ("sti\n\thlt" : : : "memory");
    }
}

void sched_block_current(void) {
    uint64_t flags = irq_save();
    struct percpu *p = percpu_self();
    struct runqueue *rq = &rqs[p->cpu_id];
    struct thread *curr_thrd = (struct thread *)p->current_thread;
    if (curr_thrd == 0 || curr_thrd == rq->idle) {
        irq_restore(flags);
        return;
    }
    spin_lock(&rq->lock);
    curr_thrd->state = THREAD_BLOCKED;
    spin_unlock(&rq->lock);
    schedule();
    irq_restore(flags);
}

int sched_wake_thread(struct thread *t) {
    if (t == 0 || rqs == 0 || t->cpu >= cpu_count)
        return 0;
    uint32_t cpu = t->cpu;
    struct runqueue *rq = &rqs[cpu];
    uint64_t flags = spin_lock_irqsave(&rq->lock);
    if (t->state != THREAD_BLOCKED) {
        spin_unlock_irqrestore(&rq->lock, flags);
        return 0;
    }
    if (t->on_cpu) {
        t->wake_pending = 1;
    } else {
        t->state = THREAD_READY;
        t->slice_left = SCHED_SLICE_TICKS;
        rq_push(rq, t);
    }
    spin_unlock_irqrestore(&rq->lock, flags);
    struct percpu *me = percpu_self();
    if (me->cpu_id != cpu) {
        struct percpu *target = percpu_by_cpu_id(cpu);
        if (target != 0)
            lapic_send_ipi(target->apic_id, SCHED_IPI_COMMAND);
    }
    return 1;
}

int sched_prepare_block_current(void) {
    uint64_t flags = irq_save();
    struct percpu *p = percpu_self();

    if (rqs == 0 || p->cpu_id >= cpu_count) {
        irq_restore(flags);
        return 0;
    }

    struct runqueue *rq = &rqs[p->cpu_id];
    struct thread *t = (struct thread *)p->current_thread;

    if (t == 0 || t == rq->idle) {
        irq_restore(flags);
        return 0;
    }

    uint64_t rq_flags = spin_lock_irqsave(&rq->lock);

    if (t->state != THREAD_RUNNING || !t->on_cpu) {
        spin_unlock_irqrestore(&rq->lock, rq_flags);
        irq_restore(flags);
        return 0;
    }

    t->state = THREAD_BLOCKED;

    spin_unlock_irqrestore(&rq->lock, rq_flags);
    irq_restore(flags);
    return 1;
}

int sched_cancel_block_current(void) {
    uint64_t flags = irq_save();
    struct percpu *p = percpu_self();

    if (rqs == 0 || p->cpu_id >= cpu_count) {
        irq_restore(flags);
        return 0;
    }

    struct runqueue *rq = &rqs[p->cpu_id];
    struct thread *t = (struct thread *)p->current_thread;

    if (t == 0) {
        irq_restore(flags);
        return 0;
    }

    uint64_t rq_flags = spin_lock_irqsave(&rq->lock);
    int ok = 0;

    if (t->on_cpu && t->state == THREAD_BLOCKED) {
        t->state = THREAD_RUNNING;
        t->wake_pending = 0;
        ok = 1;
    }

    spin_unlock_irqrestore(&rq->lock, rq_flags);
    irq_restore(flags);
    return ok;
}
