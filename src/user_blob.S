    .global user_exit_start
    .global user_exit_end
user_exit_start:
    .incbin "exit.elf"
user_exit_end:
