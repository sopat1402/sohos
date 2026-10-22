#ifndef MB2_MODULE_H
#define MB2_MODULE_H

#include <stdint.h>

int mb2_find_module(const void *mbi, uint32_t max_size, uint64_t *mod_start, uint64_t *mod_end);

#endif
