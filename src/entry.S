.intel_syntax noprefix

.section .multiboot2, "a"
.align 8

mb2_start:
    .long 0xe85250d6
    .long 0
    .long mb2_end - mb2_start
    .long -(0xe85250d6 + 0 + (mb2_end - mb2_start))

    .short 0
    .short 0
    .long 8

mb2_end:


.section .bss
.align 4096

pml4:
    .skip 4096

pdpt:
    .skip 4096

pd:
    .skip 4096

.align 16

stack_bottom:
    .skip 65536

stack_top:


.section .rodata
.align 8

gdt64:
    .quad 0
    .quad 0x00209a0000000000
    .quad 0x0000920000000000

gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64


.section .text
.code32

.global _start
.extern kmain

_start:
    cli

    cmp eax, 0x36d76289
    jne hang

    mov edi, eax
    mov esi, ebx

    mov esp, offset stack_top

    mov eax, offset pdpt
    or eax, 3
    mov dword ptr [pml4], eax

    mov eax, offset pd
    or eax, 3
    mov dword ptr [pdpt], eax

    xor ecx, ecx

1:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov dword ptr [pd + ecx*8], eax

    inc ecx
    cmp ecx, 512
    jne 1b

    mov eax, offset pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64_ptr]

    push 0x08
    push offset long_mode
    retf


.code64

long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    xor eax, eax
    mov fs, ax
    mov gs, ax

    lea rsp, [rip + stack_top]

    xor ebp, ebp

    call kmain


hang:
    cli
    hlt
    jmp hang
