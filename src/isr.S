.intel_syntax noprefix
.code64

.pushsection .rodata.isr_table, "a"
.align 8
.global isr_stub_table
isr_stub_table:
.popsection

.macro ISR_NOERR num, target=isr_common
    .pushsection .text
    .align 16
isr_stub_\num:
    push 0
    push \num
    jmp \target
    .popsection
    .pushsection .rodata.isr_table, "a"
    .quad isr_stub_\num
    .popsection
.endm

.macro ISR_ERR num, target=isr_common
    .pushsection .text
    .align 16
isr_stub_\num:
    push \num
    jmp \target
    .popsection
    .pushsection .rodata.isr_table, "a"
    .quad isr_stub_\num
    .popsection
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2, isr_common_paranoid
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8, isr_common_paranoid
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18, isr_common_paranoid
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

.section .text
isr_common:
    test qword ptr [rsp + 24], 3
    jz .Lisr_entry_kernel
    swapgs
.Lisr_entry_kernel:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov rdi, rsp
    mov rbp, rsp
    and rsp, -16
    call interrupt_dispatch
    mov rsp, rbp

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16

    test qword ptr [rsp + 8], 3
    jz .Lisr_exit_kernel
    swapgs
.Lisr_exit_kernel:
    iretq

isr_common_paranoid:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    xor r12d, r12d
    mov ecx, 0xC0000101
    rdmsr
    test edx, edx
    js .Lparanoid_kernel_gs
    swapgs
    mov r12d, 1
.Lparanoid_kernel_gs:
    mov rdi, rsp
    mov rbp, rsp
    and rsp, -16
    call interrupt_dispatch
    mov rsp, rbp

    test r12d, r12d
    jz .Lparanoid_no_swap
    swapgs
.Lparanoid_no_swap:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

.balign 16
.global irq_stub_base
irq_stub_base:
.set irq_vec, 32
.rept 224
.balign 16
    .byte 0x6a, 0x00
    .byte 0x68
    .long irq_vec
    jmp isr_common
    .set irq_vec, irq_vec + 1
.endr
