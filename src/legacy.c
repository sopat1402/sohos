#include <stdint.h>
#include "../include/legacy.h"
 
#define PIT_HZ 1193182ULL
#define PIT_MAX_CHUNK_US 50000u
#define PIC1_COMMAND 0x20u
#define PIC1_DATA 0x21u
#define PIC2_COMMAND 0xA0u
#define PIC2_DATA 0xA1u
#define PIC_REMAP_BASE 0x20u
 
static inline uint8_t legacy_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
 
static inline void legacy_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
 
static inline void legacy_io_wait(void) {
    legacy_outb(0x80, 0);
}
 
void pit_delay_us(uint32_t usec) {
    while (usec != 0) {
        uint32_t chunk = usec > PIT_MAX_CHUNK_US ? PIT_MAX_CHUNK_US : usec;
        uint16_t count = (uint16_t)((PIT_HZ * chunk + 999999ULL) / 1000000ULL);
        if (count == 0)
            count = 1;
        uint8_t old_port61 = legacy_inb(0x61);
        legacy_outb(0x61, old_port61 & (uint8_t)~0x03u);
        legacy_outb(0x43, 0xB0);
        legacy_outb(0x42, (uint8_t)(count & 0xffu));
        legacy_outb(0x42, (uint8_t)(count >> 8));
        legacy_outb(0x61, (old_port61 & (uint8_t)~0x03u) | 0x01u);
        while ((legacy_inb(0x61) & 0x20u) == 0)
            __asm__ volatile ("pause");
        legacy_outb(0x61, old_port61);
        usec -= chunk;
    }
}
 
void pic_disable(void) {
    legacy_outb(PIC1_COMMAND, 0x11);
    legacy_io_wait();
    legacy_outb(PIC2_COMMAND, 0x11);
    legacy_io_wait();
    legacy_outb(PIC1_DATA, PIC_REMAP_BASE);
    legacy_io_wait();
    legacy_outb(PIC2_DATA, PIC_REMAP_BASE + 8);
    legacy_io_wait();
    legacy_outb(PIC1_DATA, 4);
    legacy_io_wait();
    legacy_outb(PIC2_DATA, 2);
    legacy_io_wait();
    legacy_outb(PIC1_DATA, 0x01);
    legacy_io_wait();
    legacy_outb(PIC2_DATA, 0x01);
    legacy_io_wait();
    legacy_outb(PIC1_DATA, 0xFF);
    legacy_outb(PIC2_DATA, 0xFF);
}
