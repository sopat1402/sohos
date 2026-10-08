# SOHOS DEV NOTES

This is to show the thought process and is essentially a lab notebook I fill stuff in for later reference.

# Entry and initial stuff

K so entry.S will fit into gcc. I'm not writing a bootloader. GCC gives me a CPU, I set up a simple environ, long mode
and then send it to my kmain which is C. I don't need a PhD in x86 assembly and GAS's bs. QEMU to test stuff, naturally.
on hang it spins, halts and waits for an interrupt.

So in kernel.c, I don't have a printf yet. This itself is a big change. Code is usually linked to libc. So I need
to first make a putchar. But, it turns out we don't even gaf there about the stack pointer. there's a vga text buffer
and we're writing to it one way or another so why persist it at all. That's because I have the luxury of deciding
what memory even means rn. Video char* is initialised as 0xB8000 due to the way the memory cookie crumbles at the
very hardware level itself. 0x07 in putchar is the attribute for the char. ig it is stuff like foreground colour and
background colour. VGA is legacy and it's basically training wheels so that I at least have something. Eventually
there'll be a driver for video and stuff. Printf is formatted. That's a whole different beast so when I'm actually
using pixels I'll think about it.

80x25 size VGA. So, on \n it goes to the next line by doing cursor += 80 - cursor%80. Cool but standard. 0xB8000 is being
written to. That's VGA's area.

# Parsing multiboot info

GRUB gives me data. The first 8 bytes are total size and reserved. After that there's tags to tell me things like
the grub version/bootloader details, the memory map (very important), command line to have command line args
later, etc. It also gives me the framebuffer it set up so that I can eventually use a frame buffer instead of lifting
musty old VGA up from its tomb.

The code is parsing the structs sent by GRUB but now, it is just printing it. Later a frame allocation using buddy
allocation must come into picture.

# Frame allocation

I have to make a bitmap but this is pre malloc (heap) and pre filesystem. How? I have some tough af bootstrapping thing
and I'll have to lookup how to do it but basically once I have a bitmap, I have each page as 4 KiB and then I go through
available memory and make the bitmap accordingly and reserved must be marked reserved and then even the available 
memory has memory that is reserved for the kernel itself in entry.S. Hard questions but they'll take me to a whole new
level in the coding skills section.

K so in the max available address, it is actually returning the upper bound. So the highest usable address is end_addr-1.
The first pass gives me the max address and the second one will be used to fill the bitmap.
0x0000 to 0x0FFF will be the first frame. Ah there's no malloc yet so the bitmap can't be made in the kernel's stack.
That means I must find a bit of physical RAM I can use and make the bitmap there. I'm already calculating the number
of bytes it needs. But this means 3 total passes : one to get the max addr. another to find a place to park my bitmap
and then another to fill the bitmap. That means a contiguous portion of the address space based on num_bytes.
To find free slots I also need to ensure that the kernel start and end that I exposed in linker.ld are not a part of
that address space.

Ok so the third pass that needed me to calculate not only which bit is to be written but also which frame the byte (of
memory) falls into was hard af. I then spent around half an hour because I didn't put flanking spaces around the = in
my linker.ld where I exposed kernel start and end. I also made bitmap space reject / clamp  to low memory when the
num bytes is less that 1MiB due to the kernel data.
