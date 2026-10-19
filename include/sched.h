#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "thread.h"

#define SCHED_SLICE_TICKS 5u
#define SCHED_IPI_VECTOR 0x41u
#define SCHED_NO_CPU 0xFFFFFFFFu

int sched_init(void);
int sched_adopt_boot_thread(void);
__attribute__((noreturn)) void sched_idle_loop(void);
void schedule(void);
void sched_yield(void);
void sched_tick(void);
void sched_finish_switch(struct thread *prev);
uint32_t sched_pick_cpu(void);
int sched_enqueue(struct thread *t, uint32_t cpu);
int sched_cancel_block_current(void);
int sched_prepare_block_current(void);
int sched_wake_thread(struct thread *t);
struct thread *switch_to(struct thread *old, struct thread *next);
void thread_trampoline(void);
void as_switch(struct address_space *next);

#endif
