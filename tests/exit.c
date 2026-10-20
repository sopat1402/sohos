__attribute__((force_align_arg_pointer)) void _start(void) {
    __asm__ volatile ("syscall" :: "a"(60), "D"(0) : "rcx", "r11", "memory");
    for (;;) {}
}
