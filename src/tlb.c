#include <stdint.h>
#include "../include/idt.h"
#include "../include/lapic.h"
#include "../include/percpu.h"
#include "../include/tlb.h"

#define TLB_SHOOTDOWN_VECTOR 0xF2u

#define CR4_PGE   (1ULL << 7)
#define CR4_PCIDE (1ULL << 17)
#define RFLAGS_IF (1ULL << 9)

static uint64_t tlb_generation;
static uint32_t tlb_ready;

static uint32_t shootdown_next_ticket;
static uint32_t shootdown_serving_ticket;

static uint64_t tlb_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli"
        : "=r"(flags) :: "memory");
    return flags;
}

static void tlb_irq_restore(uint64_t flags) {
    if (flags & RFLAGS_IF)
        __asm__ volatile("sti" ::: "memory");
}

static int tlb_has_invpcid(void) {
    uint32_t eax = 0, ebx, ecx = 0, edx;

    __asm__ volatile("cpuid"
        : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx)
        :: "memory");

    if (eax < 7)
        return 0;

    eax = 7;
    ecx = 0;

    __asm__ volatile("cpuid"
        : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx)
        :: "memory");

    return (ebx & (1u << 10)) != 0;
}

static void tlb_flush_local_all(void) {
    if (tlb_has_invpcid()) {
        struct invpcid_desc {
            uint64_t pcid;
            uint64_t address;
        } __attribute__((aligned(16))) desc = {0, 0};

        uint64_t type = 2;

        __asm__ volatile("invpcid %[desc], %[type]"
            :: [desc] "m"(desc), [type] "r"(type)
            : "memory");

        return;
    }

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    if (cr4 & CR4_PGE) {
        uint64_t without_pge = cr4 & ~CR4_PGE;

        __asm__ volatile("mov %0, %%cr4"
            :: "r"(without_pge) : "memory");

        __asm__ volatile("mov %0, %%cr4"
            :: "r"(cr4) : "memory");

        return;
    }

    if (!(cr4 & CR4_PCIDE)) {
        uint64_t cr3;

        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3"
            :: "r"(cr3) : "memory");

        return;
    }

    uint64_t with_pge = cr4 | CR4_PGE;

    __asm__ volatile("mov %0, %%cr4"
        :: "r"(with_pge) : "memory");

    __asm__ volatile("mov %0, %%cr4"
        :: "r"(cr4) : "memory");
}

static void tlb_service_pending(struct percpu *cpu) {
    for (;;) {
        uint64_t requested =
            __atomic_load_n(&tlb_generation, __ATOMIC_ACQUIRE);

        uint64_t acknowledged =
            __atomic_load_n(&cpu->tlb_ack_generation, __ATOMIC_RELAXED);

        if (acknowledged >= requested)
            return;

        uint64_t flags = tlb_irq_save();
        tlb_flush_local_all();

        __atomic_store_n(&cpu->tlb_ack_generation,
                         requested, __ATOMIC_RELEASE);

        tlb_irq_restore(flags);

        if (__atomic_load_n(&tlb_generation, __ATOMIC_ACQUIRE) == requested)
            return;
    }
}

static void tlb_ipi_handler(struct interrupt_frame *frame) {
    (void)frame;

    struct percpu *cpu = percpu_self();

    if (cpu && cpu->self == cpu)
        tlb_service_pending(cpu);

    lapic_eoi();
}

int tlb_init(void) {
    if (__atomic_load_n(&tlb_ready, __ATOMIC_ACQUIRE))
        return tlb_cpu_online();

    if (!irq_register(TLB_SHOOTDOWN_VECTOR, tlb_ipi_handler))
        return 0;

    __atomic_store_n(&tlb_ready, 1, __ATOMIC_RELEASE);
    return tlb_cpu_online();
}

int tlb_cpu_online(void) {
    if (!__atomic_load_n(&tlb_ready, __ATOMIC_ACQUIRE))
        return 0;

    struct percpu *cpu = percpu_self();

    if (!cpu || cpu->self != cpu)
        return 0;

    if (__atomic_load_n(&cpu->tlb_online, __ATOMIC_ACQUIRE))
        return 1;

    uint64_t flags = tlb_irq_save();

    tlb_flush_local_all();

    uint64_t generation =
        __atomic_load_n(&tlb_generation, __ATOMIC_ACQUIRE);

    __atomic_store_n(&cpu->tlb_ack_generation,
                     generation, __ATOMIC_RELEASE);

    __atomic_store_n(&cpu->tlb_online, 1, __ATOMIC_RELEASE);

    tlb_irq_restore(flags);
    return 1;
}

static uint32_t tlb_shootdown_gate_acquire(void) {
    uint32_t ticket = __atomic_fetch_add(
        &shootdown_next_ticket, 1, __ATOMIC_RELAXED);

    while (__atomic_load_n(&shootdown_serving_ticket,
                           __ATOMIC_ACQUIRE) != ticket)
        __asm__ volatile("pause");

    return ticket;
}

static void tlb_shootdown_gate_release(uint32_t ticket) {
    __atomic_store_n(&shootdown_serving_ticket,
                     ticket + 1, __ATOMIC_RELEASE);
}

void tlb_shootdown(void) {
    struct percpu *self = percpu_self();

    uint64_t entry_flags;
    __asm__ volatile("pushfq; popq %0"
        : "=r"(entry_flags) :: "memory");

    if (!self || self->self != self ||
        !__atomic_load_n(&self->tlb_online, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&tlb_ready, __ATOMIC_ACQUIRE) ||
        !(entry_flags & RFLAGS_IF)) {
        __asm__ volatile("cli" ::: "memory");
        for (;;)
            __asm__ volatile("hlt");
    }

    uint32_t ticket = tlb_shootdown_gate_acquire();

    uint64_t generation = __atomic_add_fetch(
        &tlb_generation, 1, __ATOMIC_ACQ_REL);

    uint32_t count = percpu_count();

    for (uint32_t i = 0; i < count; i++) {
        struct percpu *cpu = percpu_by_cpu_id(i);

        if (!cpu || cpu == self ||
            !__atomic_load_n(&cpu->tlb_online, __ATOMIC_ACQUIRE))
            continue;

        while (!lapic_send_ipi(cpu->apic_id, TLB_SHOOTDOWN_VECTOR))
            __asm__ volatile("pause");
    }

    tlb_service_pending(self);

    for (;;) {
        int complete = 1;

        for (uint32_t i = 0; i < count; i++) {
            struct percpu *cpu = percpu_by_cpu_id(i);

            if (!cpu ||
                !__atomic_load_n(&cpu->tlb_online, __ATOMIC_ACQUIRE))
                continue;

            if (__atomic_load_n(&cpu->tlb_ack_generation,
                                __ATOMIC_ACQUIRE) < generation) {
                complete = 0;
                break;
            }
        }

        if (complete)
            break;

        __asm__ volatile("pause");
    }

    tlb_shootdown_gate_release(ticket);
}
