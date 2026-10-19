#include <stdint.h>
#include "../include/syscall.h"
#include "../include/idt.h"
#include "../include/thread.h"
#include "../include/sched.h"
#include "../include/display.h"

#define SC_MSR_EFER 0xC0000080u
#define SC_MSR_STAR 0xC0000081u
#define SC_MSR_LSTAR 0xC0000082u
#define SC_MSR_FMASK 0xC0000084u
#define SC_EFER_SCE 1ull
#define SC_FMASK 0x47700ull

extern void syscall_entry(void);

static uint64_t sc_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
    return ((uint64_t)high << 32) | low;
}

static void sc_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

void syscall_init(void) {
    sc_wrmsr(SC_MSR_EFER, sc_rdmsr(SC_MSR_EFER) | SC_EFER_SCE);
    sc_wrmsr(SC_MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
    sc_wrmsr(SC_MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    sc_wrmsr(SC_MSR_FMASK, SC_FMASK);
}

void syscall_dispatch(struct interrupt_frame *f) {
    switch (f->rax) {
    case SYS_EXIT:
        thread_exit();
    case SYS_YIELD:
        schedule();
        f->rax = 0;
        break;
    case SYS_DEBUG_NUM:
        print("[SYSCALL] ");
        print_uint(f->rdi);
        print("\n");
        f->rax = 0;
        break;
    default:
        f->rax = (uint64_t)-38;
        break;
    }
}
