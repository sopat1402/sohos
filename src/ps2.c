#include <stdint.h>
#include "../include/ps2.h"
#include "../include/spinlock.h"
#include "../include/usercpy.h"
#include "../include/wait_queue.h"
#include "../include/sched.h"

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

static struct wait_queue kbd_queue;

static spinlock_t kbd_lock;

static char ascii_map_lower[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n', 
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2B] = '\\', 
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static char ascii_map_upper[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n', 
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
    [0x28] = '"', [0x29] = '~', [0x2B] = '|', 
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' '
};

static int shift_pressed=0;
static int caps_on=0;

static inline void io_wait(void) {
    __asm__ volatile ("outb %al, $0x80");
}

void pic_init(void) {
    unsigned char a1 = inb(PIC1_DATA);
    unsigned char a2 = inb(PIC2_DATA);
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void keyboard_pic_enable(void) {
    unsigned char mask = inb(0x21);
    mask &= ~(1 << 1);
    outb(0x21, mask);
}

inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static struct kbd_buffer kbd_q = {0};

void kbd_init(){
    pic_init();
    keyboard_pic_enable();
    wait_queue_init(&kbd_queue);
}

void kbd_buffer_push(uint8_t scancode) {
    uint64_t flags=spin_lock_irqsave(&kbd_lock);
    uint16_t next_tail = (kbd_q.tail + 1) % KBD_BUFFER_SIZE;
    if (next_tail != kbd_q.head) {
        kbd_q.buffer[kbd_q.tail] = scancode;
        kbd_q.tail = next_tail;
    }
    spin_unlock_irqrestore(&kbd_lock,flags);
    wait_queue_wake_one(&kbd_queue);
}

int kbd_buffer_pop(uint8_t *scancode) {
    uint64_t flags = spin_lock_irqsave(&kbd_lock);
    while (kbd_q.head == kbd_q.tail) {
        struct wait_queue_entry wq_entry;
        wait_queue_prepare_to_wait(&kbd_queue, &wq_entry, &flags);
        spin_unlock_irqrestore(&kbd_lock, flags);
        schedule();
        flags = spin_lock_irqsave(&kbd_lock);
    }
    *scancode = kbd_q.buffer[kbd_q.head];
    kbd_q.head = (kbd_q.head + 1) % KBD_BUFFER_SIZE;
    spin_unlock_irqrestore(&kbd_lock, flags);
    return 0;
}

unsigned long kbd_read(unsigned long count,char *buf){
    unsigned long read=0;
    int done=0;
    char *curr=buf;
    while (!done){
        if (read==count) break;
        uint8_t scancode=0;
        kbd_buffer_pop(&scancode);
        if (scancode==0x2A || scancode==0x36){
            shift_pressed=1;
            continue;
        }
        else if (scancode==0xAA || scancode==0xB6){
            shift_pressed=0;
            continue;
        }
        else if (scancode==0x1C){
            done=1;
        }
        else if (scancode==0x3A){
            caps_on=!caps_on;
        }
        else if (scancode==0xBA){
            continue;
        }
        else if (scancode==0x0E){
            if (read==0) continue;
            curr--;
            read--;
            continue;
        }
        if (scancode & 0x80) continue;
        char read_char;
        if (shift_pressed){
            read_char=ascii_map_upper[scancode];
        }else{
            read_char=ascii_map_lower[scancode];
        }
        if (read_char=='\0') continue;
        if (read_char>=97 && read_char <= 122 && caps_on) read_char-=32;
        copy_to_user(curr,&read_char,sizeof(char));
        read++;
        curr++;
    }
    return read;
}
