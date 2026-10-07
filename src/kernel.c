#include <stdint.h>

static volatile uint8_t *vga = (volatile uint8_t *)0xB8000;
static uint16_t cursor = 0;

void putchar(char c){
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

void kmain(uint32_t magic, uint64_t multiboot_data){
	print("Hello from sohos's kernel!\n");
	print("For so long, I have had to scream but had no mouth.\n");
	print("You will suffer for this.\n");
}
