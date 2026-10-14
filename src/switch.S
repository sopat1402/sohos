.intel_syntax noprefix
.code64
 
.section .text
 
.global switch_to
switch_to:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rax, rdi
    mov [rdi], rsp
    mov rsp, [rsi]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
 
.global thread_trampoline
thread_trampoline:
    mov rdi, rax
    call sched_finish_switch
    sti
    mov rdi, r13
    call r12
    call thread_exit
    ud2
