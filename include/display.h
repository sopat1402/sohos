#ifndef DISPLAY
#define DISPLAY 1

#include <stdint.h>

static volatile uint8_t *vga = (volatile uint8_t *)0xB8000;
static uint16_t cursor = 0;
void putchar(char c);

void print(const char *str);

void print_hex(uint64_t value);

void print_uint(uint64_t value);

void print_size(uint64_t value);

#endif
