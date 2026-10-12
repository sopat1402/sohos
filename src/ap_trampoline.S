.intel_syntax noprefix

.section .boot.text.ap_trampoline, "ax"
.code16

.global ap_trampoline_start
.global ap_trampoline_end

/*
 * SIPI starts execution at offset 0 of this page.
 * Handoff parameters are at page offset 0xF00:
 *   +0  trampoline physical base (u32)
 *   +4  final CR3 physical address (u32)
 *   +8  AP stack virtual top (u64)
 *   +16 AP C entry virtual address (u64)
 *   +24 APIC ID (u32)
 *
 * The code must stay below offset 0xF00.
 */
ap_trampoline_start:
    cli
    cld

    /* In real mode, CS base is the SIPI page physical address. */
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, 0x0ef0

    movzx ebp, ax
    shl ebp, 4                  /* EBP = trampoline physical base */

    /* Patch the GDT base to its copied physical address. */
    mov eax, ebp
    add eax, ap_tramp_gdt - ap_trampoline_start
    mov dword ptr [ap_tramp_gdt_ptr - ap_trampoline_start + 2], eax

    /* Patch a far pointer to the 32-bit protected-mode label. */
    mov eax, ebp
    add eax, ap_tramp_pm32 - ap_trampoline_start
    mov dword ptr [ap_tramp_pm32_ptr - ap_trampoline_start], eax
    mov word ptr [ap_tramp_pm32_ptr - ap_trampoline_start + 4], 0x18 //change here

    lgdt [ap_tramp_gdt_ptr - ap_trampoline_start]

    /* Enter 32-bit protected mode. */
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    /* ljmp m16:32 using a 32-bit offset at a 16-bit DS displacement. */
    .byte 0x66, 0xff, 0x2e
    .word ap_tramp_pm32_ptr - ap_trampoline_start

.code32
ap_tramp_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    /* Rebase the temporary stack after SS became flat. */
    mov esp, ebp
    add esp, 0x0ef0

    /* Enable PAE, load the kernel's final page tables, then enable long mode. */
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, dword ptr [ebp + 0xF04]
    mov cr3, eax

    mov ecx, 0xC0000080          /* IA32_EFER */
    rdmsr
    or eax, 1 << 8              /* EFER.LME */
    wrmsr
    /* Prepare the far pointer before paging starts. */
    mov eax, ebp
    add eax, ap_tramp_long64 - ap_trampoline_start
    mov dword ptr [ebp + ap_tramp_long64_ptr - ap_trampoline_start], eax
    mov word ptr [ebp + ap_tramp_long64_ptr - ap_trampoline_start + 4], 0x08

    mov eax, cr0
    or eax, 0x80000001          /* PG | PE */
    mov cr0, eax

    /* ljmp m16:32 [EBP + disp32] */
    .byte 0xff, 0xad
    .long ap_tramp_long64_ptr - ap_trampoline_start

.code64
ap_tramp_long64:
    cli
    cld

    /* EBP still contains the low trampoline physical base. */
    mov rsp, qword ptr [rbp + 0xF08]
    and rsp, -16
    sub rsp, 8
    mov qword ptr [rsp], 0      /* noreturn C entry; dummy return slot */

    mov edi, dword ptr [rbp + 0xF18]  /* first argument: APIC ID */
    mov rax, qword ptr [rbp + 0xF10]  /* AP C entry */
    jmp rax

.align 8
ap_tramp_gdt:
    .quad 0
    .quad 0x00af9a000000ffff  /* 0x08: 64-bit code; final jump and IDT */
    .quad 0x00cf92000000ffff  /* 0x10: flat data */
    .quad 0x00cf9a000000ffff  /* 0x18: flat 32-bit transition code */
ap_tramp_gdt_end:

ap_tramp_gdt_ptr:
    .word ap_tramp_gdt_end - ap_tramp_gdt - 1
    .long 0

ap_tramp_pm32_ptr:
    .long 0
    .word 0

ap_tramp_long64_ptr:
    .long 0
    .word 0

ap_trampoline_end:
