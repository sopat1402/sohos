#ifndef DISPLAY
#define DISPLAY 1

#include <stdint.h>

void putchar(char c);

void print(const char *str);

void print_hex(uint64_t value);

void print_uint(uint64_t value);

void print_size(uint64_t value);

#endif
