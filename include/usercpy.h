#ifndef USERCPY
#define USERCPY

#include <stdint.h>

struct ex_table_entry {
    int32_t insn_offset;
    int32_t fixup_offset;
};

uint64_t search_exception_tables(uint64_t fault_rip);
void enable_smep_smap(void);
unsigned long copy_from_user(void *to, const void *from, unsigned long n);
unsigned long copy_to_user(void *to, const void *from, unsigned long n);

#endif
