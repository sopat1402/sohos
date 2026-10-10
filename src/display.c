#include <stdint.h>

static volatile uint8_t *vga = (volatile uint8_t *)0xB8000;
static uint16_t cursor = 0;

void putchar(char c){
	if (cursor >= 80 * 25)
		return;
	if (c == '\n') {
		cursor += 80 - (cursor % 80);
		return;
	}
	vga[cursor * 2] = c;
	vga[cursor * 2 + 1] = 0x07;
	cursor++;
}

void print(const char *str){
    while (*str)
        putchar(*str++);
}

void print_hex(uint64_t value) {
    char buffer[17];
    int i = 16;
    buffer[16] = '\0';
    do {
        uint8_t digit = value & 0xF;
        buffer[--i] = digit < 10
            ? '0' + digit
            : 'A' + (digit - 10);
        value >>= 4;
    } while (value != 0);
    print(&buffer[i]);
}

void print_uint(uint64_t value){
	char buffer[21];
	int i = 20;
	buffer[i] = '\0';
	do {
		buffer[--i] = '0' + (value % 10);
		value /= 10;
	} while (value != 0);
	print(&buffer[i]);
}

void print_size(uint64_t value) {
    char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int i = 0;
    uint64_t scaled = value;

    while (scaled >= 1024 && i < 5) {
        scaled /= 1024;
        i++;
    }

    uint64_t whole = scaled;
    uint64_t remainder = value;

    for (int j = 0; j < i; j++)
        remainder /= 1024;

    // Keep one decimal digit using integer arithmetic.
    uint64_t divisor = 1;
    for (int j = 0; j < i; j++)
        divisor *= 1024;

    uint64_t tenths = (value * 10 / divisor) % 10;

    print_uint(whole);
    print(".");
    print_uint(tenths);
    print(" ");
    print(units[i]);
}

void display_set_cursor(uint16_t row, uint16_t col){
    cursor = row * 80 + col;
}
