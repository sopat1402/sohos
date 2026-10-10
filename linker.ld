OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

PHDRS
{
    boot     PT_LOAD FLAGS(5);
    bootdata PT_LOAD FLAGS(6);
    text     PT_LOAD FLAGS(5);
    rodata   PT_LOAD FLAGS(4);
    data     PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x00100000;
    __image_phys_start = .;

    .boot ALIGN(0x1000) :
    {
        KEEP(*(.multiboot2))
        *(.boot.text .boot.text.*)
        *(.boot.rodata .boot.rodata.*)
    } :boot

    .boot.bss ALIGN(0x1000) (NOLOAD) :
    {
        *(.boot.bss .boot.bss.*)
    } :bootdata

    __boot_phys_end = .;
    __kernel_phys_start = ALIGN(__boot_phys_end, 0x200000);

    . = KERNEL_VMA;
    __kernel_virt_start = .;

    .text ALIGN(0x1000) : AT(__kernel_phys_start)
    {
        *(.text .text.*)
    } :text

    .rodata ALIGN(0x1000) :
        AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 0x1000))
    {
        *(.rodata .rodata.*)
    } :rodata

    .data ALIGN(0x1000) :
        AT(ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 0x1000))
    {
        *(.data .data.*)
        *(.got .got.*)
    } :data

    .bss ALIGN(0x1000) (NOLOAD) :
        AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 0x1000))
    {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(0x1000);
    } :data

    __kernel_virt_end = .;
    __kernel_phys_end =
        __kernel_phys_start + (__kernel_virt_end - __kernel_virt_start);

    kernel_start = __kernel_virt_start;
    kernel_end = __kernel_virt_end;

    __image_phys_end = __kernel_phys_end;

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
        *(.comment)
    }

    ASSERT((__kernel_phys_start & 0x1FFFFF) == 0,
        "kernel physical load address must be 2 MiB aligned")
    ASSERT(__kernel_phys_end <= 0x40000000,
        "kernel image must fit below the 1 GiB bootstrap limit")
    ASSERT((__kernel_virt_end - __kernel_virt_start) <= 0x40000000,
        "kernel image exceeds the bootstrap high mapping")
}
