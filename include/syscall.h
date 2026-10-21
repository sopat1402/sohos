#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

struct interrupt_frame;

#define SYS_YIELD	24
#define SYS_DEBUG_NUM	500
#define SYS_READ	0
#define SYS_WRITE	1
#define SYS_EXIT	60
#define SYS_WAIT	61
#define SYS_SPAWN	3

void syscall_init(void);
void syscall_dispatch(struct interrupt_frame *f);

#endif
