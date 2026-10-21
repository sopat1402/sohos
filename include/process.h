#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "wait_queue.h"

struct address_space;
struct thread;

#define PROCESS_RUNNING 1
#define PROCESS_ZOMBIE  0

struct process {
    uint32_t pid;
    uint32_t state;
    int32_t exit_code;
    struct process *parent;
    struct address_space *as;
    struct thread *main_thread;
    struct process *children;
    struct process *sibling;
    struct wait_queue child_wq;
};

struct process *process_create(const char *name, const void *elf_data, uint64_t elf_size);
void sys_exit(int code);
int32_t process_wait(int32_t pid, int *status_out);
struct process *current_process(void);
struct process *process_get_by_pid(uint32_t pid);
int32_t process_wait(int32_t pid, int *status_out);

#endif
