# SOHOS DEV NOTES

This is to show the thought process and is essentially a lab notebook I fill stuff in for later reference.

# Entry and initial stuff

## Entry into long mode

K so entry.S will fit into gcc. I'm not writing a bootloader. GCC gives me a CPU, I set up a simple environ, long mode
and then send it to my kmain which is C. I don't need a PhD in x86 assembly and GAS's bs. QEMU to test stuff, naturally.
on hang it spins, halts and waits for an interrupt.

## VGA display

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

## Making a bitmap

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

## New PML4 tree

So, I was going to do buddy allocation. But the issue is, for the initial setup I capped my paging tables to 1 GiB but
now I have only 1 PDPT and 1 PML4. I don't want to bake the size in either cause that will just need a refactor later.
One PDPT can point to 512 entries. But, my PD points to the page directly instead of having more in front of the PDPT.
Hence the 1 GiB cap I have to fix. I'm reserving space when I do .skip 4096. PD have the 0x83 byte set for PS. That's
one of the main limits. What a mindfuck.

ugh why the fuck do I even have 2 MiB pages :( why not 4 KiB all the way? because I chose the get into 64-bit mode 
with the minimum amount of bullshit route. This is why simple bootstrapping always (over every project) comes back to
bite me in the ass and spit in my face after. After some reading I have found that huge pages are a nice optimisation
but I need to be able to have 4 KiB pages too. Also more page tables. IDK maybe I can change it once I'm in long mode?
idk. Nah that's stupid. Welp that's how it's done.
My 1 GiB shitty entry.S doesn't need to be changed. I'll add comfy C code to then just make new page tables using 
the bootloader data where it tells me how much ram I can use.

With the -4G flag, I printed max_addr and finally also understood why despite having 32 GB it says 31 on my laptop
and that's because there's a hole made for PCI BARs and such. 5368709120 is the max_addr, which is exactly 5 GiB.
So there's 1 GiB extra.

Okay I made an alloc_frame function that is just there for the bootstrapping. Allocates 3 free frames since I haven't
written a buddy allocator yet. Don't need one for just 3. I'm saving the addresses of them to construct an identity
copy of the boot tree. Also as of now the address mapping is identity but soon it won't be and it'll be a uint64_t.
So initially for the identity tree (same structure), my pml4 will point to pdpt,pdpt will point to pd, pd to 2MiB pages,
which means no PT and 0x83 is ORed in there somewhere. I'll have to look up where. Since I'm zeroing everything else, 
it'll  say not available. So if something goes wrong, it'll triple fault. I'll have to unfortunately write assembly
again to make cr3 point to my new pml4. The PD needs to have i*0x200000 ORed with 0x83 to say huge 2MiB pages. The
0x200000 is 2MiB and the i is the address of the word. 512 times. That's because the write is a uint64_t and 512*8
is 4096. So... I needed help with the inline assembly function to switch the cr3. But it didn't triple fault so yeah
it worked.

I now am adding a page table. pt[0] will be 0 for null pointer to fault. I am mapping kstart and kend into my page
table by calculating the page range and its physical address. Then, pt[page]=(page*4096)|0x3. The 0x3 says present and
writable but once it is working I'll change it to read only. I'm calculating the wrong page table index for my kernel.
I'm currently calculating just address/4096, which is physical frame. Wrong because I need a page table index. My
linker starts the kernel at 1 MiB or 0x100000.frame_number = pd_index * 512 + pt_index. 
K so now pd[0] has 4 KiB. It isn't a dynamic change yet. I guess for now I'm blowing the whole huge page thing out of 
proportion. 

I'm able to discover RAM now and total usable ram using memory_stats. I just need to actually address it. I'm having a 
pretty rough time with all of this. It isn't even with a malloc so everything I have learned so far doesn't apply. I
think once I have proper allocation and kmalloc I'll have a much easier time. 
I read some kernel code to figure out just how I'm supposed to use the available memory. Glad I did because there's a lot
of alien stuff. That musty little print_size was using GB but calculating GiB. Made me crash out for a while because I
was searching for the bug in my kernel. Anyways, it now also prints the decimal. 

Ok so my number of frames also increased. I'm testing my editing the make with different -m flags. OK so the values do
in fact scale with changing -m flags in the qemu command.

## IDT

IDT needs to be made for page faults and triple faults to be actually readable stuff. I found a lot of assembly code
for it but had to filter it and find what would actually work. There's idt.S making the low level functions I need and
an idt.c for the plugin code.

- Double faults on a bad stack still triple-fault. -> Can be done later with TSS and IST stack for vector 8.
- Vectors 32 and up aren’t wired -> APIC or PIC
- The handler halts and never returns -> Interrupts

## Memory map refactor

I made a multiboot.h to not redefine structs for multiboot and to clean up the code. Then made memory_regions.c which
lets me use this memory regions struct and merge and split them as needed. Memory_map.c now doesn't parse mutliboot
info over and over but instead uses what memory_regions_init gives, where it parses and makes memory regions.
