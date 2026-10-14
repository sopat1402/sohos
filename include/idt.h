#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IRQ_VECTOR_FIRST 32u
#define IRQ_VECTOR_SPURIOUS 0xFFu

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*irq_handler_t)(struct interrupt_frame *frame);

void idt_init(void);
void idt_load(void);
int irq_register(uint8_t vector, irq_handler_t handler);
void irq_unregister(uint8_t vector);
uint64_t irq_unhandled_count(uint8_t vector);

#endif
