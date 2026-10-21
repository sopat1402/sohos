#ifndef PS2
#define PS2

#include <stdint.h>

#define KBD_BUFFER_SIZE 256

struct kbd_buffer {
    uint8_t buffer[KBD_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
};

void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void kbd_buffer_push(uint8_t scancode);
int kbd_buffer_pop(uint8_t *scancode);
unsigned long kbd_read(unsigned long count,char *buf);
void kbd_init();

#endif
