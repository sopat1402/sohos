#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

struct thread;

struct thread *process_create(const char *name, const void *elf_data, uint64_t elf_size);

#endif
