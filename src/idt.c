#include <stdint.h>
#include "../include/idt.h"
#include "../include/display.h"
#include "../include/thread.h"
#include "../include/usercpy.h"
 
#define KERNEL_CODE_SELECTOR 0x08
#define GATE_INTERRUPT 0x8E
#define IRQ_STUB_SIZE 16u
#define IDT_VECTORS 256u
 
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
 
extern uint64_t isr_stub_table[];
extern char irq_stub_base[];
 
static struct idt_entry idt[IDT_VECTORS] __attribute__((aligned(16)));
static irq_handler_t irq_handlers[IDT_VECTORS];
static uint64_t unhandled_counts[IDT_VECTORS];
 
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
__attribute__((noreturn)) static void panic(const char *message) {
    __asm__ volatile ("cli");
    print("\nSCHED PANIC: ");
    print(message);
    for (;;)
        __asm__ volatile ("hlt");
}
static void idt_set_gate(int vector, uint64_t handler, uint8_t ist, uint8_t type_attr){
    idt[vector].offset_low=(uint16_t)(handler&0xFFFF);
    idt[vector].selector=KERNEL_CODE_SELECTOR;
    idt[vector].ist=ist;
    idt[vector].type_attr=type_attr;
    idt[vector].offset_mid=(uint16_t)((handler>>16)&0xFFFF);
    idt[vector].offset_high=(uint32_t)(handler>>32);
    idt[vector].reserved=0;
}

static void exception_handler(struct interrupt_frame *frame) {
    if (frame->vector == 14 && !(frame->error & 4)) {
        uint64_t fixup = search_exception_tables(frame->rip);
        if (fixup) {
            frame->rip = fixup;
            return;
        }
    }

    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    display_set_cursor(19, 0);
    print("EXCEPTION ");
    print_uint(frame->vector);
    print(": ");
    print(frame->vector < 32 ? exception_names[frame->vector] : "Unknown");

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

    if (frame->vector == 14) {
        int is_present = (frame->error & 1) != 0;
        int is_write = (frame->error & 2) != 0;
        int is_user = (frame->error & 4) != 0;

        print("\nPage Fault: CR2=0x");
        print_hex(cr2);
        print(is_present ? " [protection]" : " [not-present]");
        print(is_write ? " [write]" : " [read]");
        print(is_user ? " [user]\n" : " [supervisor]\n");

        if (!is_user)
            panic("Kernel page fault");

        if (cr2 < 0x1000 || cr2 >= 0x0000800000000000ULL)
            print("[USER] Invalid memory access, killing thread.\n");
        else
            print("[USER] Demand paging / stack expansion needed.\n");

        thread_exit();
    }

    if ((frame->cs & 3) == 3 && frame->vector < 32 &&
        frame->vector != 2 && frame->vector != 8 && frame->vector != 18) {
        thread_exit();
    }

    if (frame->vector == 11 && (frame->error & 2)) {
        print("\nno handler for vector ");
        print_uint(frame->error >> 3);
    }

    for (;;)
        __asm__ volatile ("cli\n\thlt");
}

void interrupt_dispatch(struct interrupt_frame *frame);
 
void interrupt_dispatch(struct interrupt_frame *frame){
    if (frame->vector < IRQ_VECTOR_FIRST) {
        exception_handler(frame);
        return;
    }
    irq_handler_t handler = __atomic_load_n(&irq_handlers[frame->vector], __ATOMIC_ACQUIRE);
    if (handler) {
        handler(frame);
        return;
    }
    __atomic_fetch_add(&unhandled_counts[frame->vector], 1, __ATOMIC_RELAXED);
}
 
int irq_register(uint8_t vector, irq_handler_t handler){
    if (vector < IRQ_VECTOR_FIRST || handler == 0)
        return 0;
    irq_handler_t expected = 0;
    return __atomic_compare_exchange_n(&irq_handlers[vector], &expected, handler, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}
 
void irq_unregister(uint8_t vector){
    if (vector < IRQ_VECTOR_FIRST)
        return;
    __atomic_store_n(&irq_handlers[vector], 0, __ATOMIC_RELEASE);
}
 
uint64_t irq_unhandled_count(uint8_t vector){
    return __atomic_load_n(&unhandled_counts[vector], __ATOMIC_RELAXED);
}
 
static struct idt_pointer idt_ptr;
 
void idt_load(void)
{
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr) : "memory");
}
 
void idt_init(void){
    for (int i = 0; i < 32; i++){
        idt_set_gate(i, isr_stub_table[i], 0, GATE_INTERRUPT);
    }
    idt_set_gate(8,isr_stub_table[8],1,GATE_INTERRUPT); //i'm making vector 8 use ist1
    uint64_t stub = (uint64_t)(uintptr_t)irq_stub_base;
    for (uint32_t i = IRQ_VECTOR_FIRST; i < IDT_VECTORS; i++)
        idt_set_gate((int)i, stub + (uint64_t)(i - IRQ_VECTOR_FIRST) * IRQ_STUB_SIZE, 0, GATE_INTERRUPT);
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)(uintptr_t)idt;
    idt_load();
}
