#include <stdint.h>
#include "../include/idt.h"
#include "../include/display.h"

#define KERNEL_CODE_SELECTOR 0x08
#define GATE_INTERRUPT 0x8E

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

extern uint64_t isr_stub_table[];

static struct idt_entry idt[256] __attribute__((aligned(16)));

static const char *exception_names[32] = {
    "Divide error", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating-point", "Alignment check", "Machine check", "SIMD floating-point",
    "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security", "Reserved"
};

static void idt_set_gate(int vector, uint64_t handler, uint8_t ist, uint8_t type_attr){
    idt[vector].offset_low=(uint16_t)(handler&0xFFFF);
    idt[vector].selector=KERNEL_CODE_SELECTOR;
    idt[vector].ist=ist;
    idt[vector].type_attr=type_attr;
    idt[vector].offset_mid=(uint16_t)((handler>>16)&0xFFFF);
    idt[vector].offset_high=(uint32_t)(handler>>32);
    idt[vector].reserved=0;
}

void idt_init(void){
    for (int i=0;i<32;i++)
        idt_set_gate(i,isr_stub_table[i],0,GATE_INTERRUPT);

    struct idt_pointer pointer={sizeof(idt)-1,(uint64_t)(uintptr_t)idt};
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
}

void exception_handler(struct interrupt_frame *frame){
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    display_set_cursor(19,0);
    print("EXCEPTION ");
    print_uint(frame->vector);
    print(": ");
    print(frame->vector<32 ? exception_names[frame->vector] : "Unknown");

    print("\nERR=0x");
    print_hex(frame->error);
    print(" RIP=0x");
    print_hex(frame->rip);
    print(" CS=0x");
    print_hex(frame->cs);

    print("\nRSP=0x");
    print_hex(frame->rsp);
    print(" RFLAGS=0x");
    print_hex(frame->rflags);

    if (frame->vector==14){
        print("\nCR2=0x");
        print_hex(cr2);
        print((frame->error&1) ? " protection" : " not-present");
        print((frame->error&2) ? " write" : " read");
        print((frame->error&4) ? " user" : " supervisor");
        if (frame->error&8)
            print(" reserved-bit");
        if (frame->error&16)
            print(" instruction-fetch");
    }

    if (frame->vector==11 && (frame->error&2)){
        print("\nno handler for vector ");
        print_uint(frame->error>>3);
    }

    for (;;)
        __asm__ volatile ("cli\n\thlt");
}
