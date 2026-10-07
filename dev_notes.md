# Init

K so entry.S will fit into gcc. I'm not writing a bootloader. GCC gives me a CPU, I set up a simple environ, long mode
and then send it to my kmain which is C. I don't need a PhD in x86 assembly and GAS's bs. QEMU to test stuff, naturally.
on hang it spins, halts and waits for an interrupt.

So in kernel.c, I don't have a printf yet. This itself is a big change. Code is usually linked to libc. So I need
to first make a putchar. But, it turns out we don't even gaf there about the stack pointer. there's a vga text buffer
and we're writing to it one way or another so why persist it at all. That's because I have the luxury of deciding
what memory even means rn. Video char* is initialised as 0xB8000 due to the way the memory cookie crumbles at the
very hardware level itself. 0x07 in putchar is the attribute for the char. ig it is stuff like foreground colour and
background colour. VGA is legacy and it's basically training wheels so that I at least have something. Eventually
there'll be a driver for video and stuff.

80x25 size VGA. So, on \n it goes to the next line by doing cursor += 80 - cursor%80. Cool but standard. 0xB8000 is being
written to. That's VGA's area.

# Parsing multiboot info

GRUB gives me data. The first 8 bytes are total size and reserved. After that there's tags to tell me things like
the grub version/bootloader details, the memory map (very important), command line to have command line args
later, etc. It also gives me the framebuffer it set up so that I can eventually use a frame buffer instead of lifting
musty old VGA up from its tomb.

The code is parsing the structs sent by GRUB but now, it is just printing it. Later a frame allocation using buddy
allocation must come into picture.
