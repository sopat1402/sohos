.intel_syntax noprefix
.code64

.set PERCPU_OFF_TSS_RSP0, 132
.set PERCPU_OFF_USER_RSP, 240

.section .text
.global syscall_entry
syscall_entry:
    swapgs
    mov qword ptr gs:[PERCPU_OFF_USER_RSP], rsp
    mov rsp, qword ptr gs:[PERCPU_OFF_TSS_RSP0]
    push 0x1B
    push qword ptr gs:[PERCPU_OFF_USER_RSP]
    push r11
    push 0x23
    push rcx
    push 0
    push 0x80
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
    sti
    call syscall_dispatch
    cli
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

    mov rcx, qword ptr [rsp]
    mov r11, rcx
    shl r11, 16
    sar r11, 16
    cmp r11, rcx
    jne .Lsyscall_iret
    cmp qword ptr [rsp + 8], 0x23
    jne .Lsyscall_iret
    mov r11, qword ptr [rsp + 16]
    mov rsp, qword ptr [rsp + 24]
    swapgs
    sysretq

.Lsyscall_iret:
    test qword ptr [rsp + 8], 3
    jz .Lsyscall_iret_kernel
    swapgs
.Lsyscall_iret_kernel:
    iretq
