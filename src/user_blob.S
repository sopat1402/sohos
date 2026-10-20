    .section .rodata
    .global user_hello_start
    .global user_hello_end
user_hello_start:
    .incbin "hello.elf"
user_hello_end:
