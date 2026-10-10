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


.section .boot.bss, "aw", @nobits
.align 4096

pml4:
    .skip 4096
low_pdpt:
    .skip 4096
low_pd:
    .skip 4096
high_pdpt:
    .skip 4096
high_pd:
    .skip 4096
boot_tables_end:

.align 16
boot_stack_bottom:
    .skip 65536
boot_stack_top:


.section .boot.rodata, "a"
.align 8

gdt64:
    .quad 0
    .quad 0x00209a0000000000
    .quad 0x0000920000000000
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64


.section .boot.text, "ax"
.code32

.global _start
.extern idt_init
.extern __kernel_phys_start
.extern __kernel_phys_end

_start:
    cli

    cmp eax, 0x36d76289
    jne boot_hang

    mov esi, ebx
    mov esp, offset boot_stack_top

    cld
    xor eax, eax
    mov edi, offset pml4
    mov ecx, (boot_tables_end - pml4) / 4
    rep stosd

    mov eax, offset low_pdpt
    or eax, 3
    mov dword ptr [pml4], eax

    mov eax, offset low_pd
    or eax, 3
    mov dword ptr [low_pdpt], eax

    xor ecx, ecx
1:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov dword ptr [low_pd + ecx*8], eax
    mov dword ptr [low_pd + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne 1b

    mov eax, offset high_pdpt
    or eax, 3
    mov dword ptr [pml4 + 511*8], eax

    mov eax, offset high_pd
    or eax, 3
    mov dword ptr [high_pdpt + 510*8], eax

    mov edi, offset high_pd
    mov eax, offset __kernel_phys_start
    mov edx, offset __kernel_phys_end
    sub edx, eax
    add edx, 0x1fffff
    shr edx, 21
    xor ecx, ecx
2:
    mov ebx, eax
    or ebx, 0x83
    mov dword ptr [edi + ecx*8], ebx
    mov dword ptr [edi + ecx*8 + 4], 0
    add eax, 0x200000
    inc ecx
    cmp ecx, edx
    jne 2b

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

boot_hang:
    cli
    hlt
    jmp boot_hang


.code64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax

    mov edi, 0x36d76289
    mov esi, esi

    push rdi
    push rsi
    movabs rax, OFFSET FLAT:idt_init
    call rax
    pop rsi
    pop rdi

    movabs rax, OFFSET FLAT:higher_half_entry
    jmp rax


.section .text.boot_high, "ax"
.code64

.global higher_half_entry
.extern kmain
.extern kernel_stack_top
.type higher_half_entry, @function

higher_half_entry:
    movabs rax, OFFSET FLAT:kernel_stack_top
    mov rsp, rax
    and rsp, -16
    xor ebp, ebp
    call kmain

1:
    cli
    hlt
    jmp 1b
.size higher_half_entry, .-higher_half_entry


.section .bss, "aw", @nobits
.align 16
.global kernel_stack_bottom
.global kernel_stack_top

kernel_stack_bottom:
    .skip 65536
kernel_stack_top:
