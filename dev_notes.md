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

# Memory

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

## Physical to Virtual addresses

K so currently it is an identity map. But my kernel is holding uint8_t* pointers and they'll go stale when an offset
changes because I want to eventually shift to a higher half kernel.
phys_to_virt and virt_to_phys are for addresses in the direct map. kernel_virt_to_phys is for symbols inside the kernel 
image (kernel_start, kernel_end), which will use a different formula once the kernel moves to the higher half. It’s the
identity today, but every kernel-symbol use goes through it so there’s one function to change then.

I'll change it later when I want to make it higher half.

## Final translator switch

I need to construct new page tables and have direct mapping. Nested per GiB loops are gone from the paging.c code.
Instead, there's map_4k and map_2m. A few API changes in new tree where it takes kstart and kend instead of highest
usable address.
new_tree builds a direct map of every region in the memory-region array at HHDM_BASE, maps VGA explicitly (uncached) 
at HHDM_BASE + 0xB8000, and builds a temporary identity bridge for the kernel image and the VGA page so execution 
survives the cr3 write. RAM is no longer identity mapped, and page 0 stays unmapped
Right after write_cr3, new_tree sets the phys_to_virt offset to HHDM_BASE. The signature changed to take the kernel's 
physical range instead of the bitmap pointers and highest address. Overlap mistakes return 0 instead of overwriting 
existing entries. Flags are present and writable only. NX and per-section permissions come later

I tested a leak too. It does give a page fault with exception 14 at the end so that means it reached there and can
process leaks with the new addresses.

## Kernel to higher half

😭 that little shit is still using an identity bridge. This memory allocation bit is a total boss fight. It'll be so
much smoother once I have a kmalloc and SLUB and whatnot. I partly stole code and partly wrote a little bit of assembly
(I've been studying). The bootstrap still starts low and it then deletes the old page tables and the kernel physical
address may be low but the virtual address is high.

So changes to entry.S and linker.ld. linker.ld tells the linker where to assemble each kernel section. low memory
bootstrap is .boot.text - _start, .boot.bss (bootstrap page tables and stack), .boot.rodata (GDT). It starts around
1 MiB.

The high virtual memory VMA starts at 0xFFFFFFFF80000000 and has .text-kmain, .rodata, .bss, .data

switch happened here:

movabs rax, OFFSET FLAT:higher_half_entry
jmp rax

0xFFFFFFFF80000000 → physical 0x200000 or is it 0xFFFF800000000000?
That's the mapping for the start of the kernel image. The page-table entries use 2 MiB pages, so each entry 
maps a 2 MiB physical region to a corresponding virtual region

Then, comes the magical stack pointer shift:
movabs rax, OFFSET FLAT:kernel_stack_top
mov rsp, rax
and rsp, -16

this shifts the stack from a low bootstrap stack to the high kernel one. 

Essentially, I created a new virtual address that points to the same physical address, made the mapping work and then
jumped to the new virtual address. The kmain RIP was 0xFFFFFFFF80000626, which is in the high virtual memory zone.

## Buddy Allocator

This is massive code. It must not only merge zones of low order when possible but must also reclaim the original
bitmap's space as it was only needed for the initial frame allocation for a new tree and stack.

So, here's the boot sequence:

1) memory_regions_init() reads the Multiboot map and records usable physical ranges
2) The bitmap starts with frames unavailable, then mark_free_memory() marks usable frames free and reserves the 
    kernel image, bitmap, and Multiboot information
3) new_tree() uses the bitmap to allocate its page-table frames. At this point the HHDM is not active yet. HHDM is
    higher half direct map and has an offset of 0XFFFF800000000000.
4) new_tree() switches to the new page tables and sets hhdm_offset
5) buddy_init() reads the bitmap’s frame status, builds the buddy free lists, and returns the now useless bitmap 
    pages to the buddy allocator.

A block’s buddy is the other half of the larger block it could combine with. For a block of 2^order pages, its buddy 
is found by xor of its physical address with the block size. They can merge only when both blocks are free and the same 
order and in the same usable region.

Physical page 0 is not usable. There is one linked list per order. Its head is a physical address, zero means the 
order has no free blocks.

make_state, state_kind, state_order pack a block's kind, order into a single byte. They are then used to extract it
too.

The rest is either direct implementation from a youtube video explaining the algorithm or a helper for metadata to
track pages or to return bitmap pages.

# Multi core support

Before slab I have to add SMP, read the APIC MADT, get the local APIC data and start APs. I will have to use a spinlock
on the buddy allocator so that many cores running this don't turn my page table into abstract art.

K so I added a simple spinlock.h ; I was surprised tbh that atomics work here and I guess all features don't because
they need libatomics, which isn't there at this level. Spinlock functions are static inline, the lock calls x86 pause
using assembly. I used to wonder why spinlocks. Because mutexes aren't low level enough and have a whole wait queue.

Zeroing a block has to happen AFTER unlocking otherwise zeroing a large block while holding the spinlock will burn
CPU and slow the whole thing down.

I now have to read APIC data so must read RSDP, which will eventually lead me to a MADT table. Before sending SIPI, I
have to allocate the AP stack and handoff data while the BSP is still active.

cpu_probe_preapic reads cpuid to see if there's ap and if it is supported and looks at the IA32 APIC BASE but does not
change it (read only). the mb2_find_acpi_rsdp function is explaining itself through its name : it is checking the
multiboot 2 info for rsdp data. it has 3 kinds of returns:

 -  1 valid RSDP found, copied to out
 -  0: no ACPI RSDP tag
 - -1: bad Multiboot info / invalid RSDP tag

CPUID.1 edx[5] = MSR, EDX[9] = APIC.
CPUID.1 ecx[21] = x2APIC support
Legacy 8-bit APIC ID fallback from CPUID.1:EBX[31:24]

There's an 8 byte multiboot info (already designed and used) and then the mb2 info I need starts at 8 byte boundaries.
I got the data above from a loooong deep dive on kernel.org and anyways, I made it prefer v2 rsdp data but if that isn't
there it takes v1.

With qemu, the above says the CPU vendor is AuthenticAMD. Is it actually that for my CPU? idgaf. anyways, now I need to
follow the rsdp to acpi tables and discover the madt. so far I am not discovering other CPUs yet. the LAPIC base read 
here is only the BSP’s current MSR value. Haven't enabled it yet or even mapped it.
If I have to read another architecture document I'm going to cry myself to sleep.

NOOO I can't directly set up a trampoline at a low physical address because I'm identity mapping that first 1 GiB
initially but then new_tree doesn't preserve that so I'm not reading the AP data and the old page tables go away too.
Trampoline needs a separate reservation and allocation path. Still can't give SIPI.

If idt_init() constructs the shared IDT on the BSP, then having it call idt_load() at the end is nice for the BSP. 
The issue is LIDT loads IDTR on the current logical processor. It doesn't broadcast the IDT to every CPU.

I feel like shit for relying on kernel.org so much. I had to write a lot of assembly (assisted) and in my entry am
requesting the data too before mb end.

For some reason, ap_prepare_early is giving me -1. I ran qemu with -smp 2 but no dice. it's fucked. smp debugging. may
as well say bye to a week. unless I get lucky.

0xBFFE2390 is above 0x40000000 (1 GiB), so ap_acpi_table_at() rejects the XSDT before reading it. Ik that because
I made the root table thing print the address and an error message if root==0. I'll map 4 GiB ig. K that fixed it but
like whack a mole, a new thing showed up : it says starting cpu cores but prints no success or error message after and
qemu just pauses.

The BSP is prolly stuck in ap_wait_online(). it spins without printing. The AP isn’t reaching the code that raises
ap_online_count
trampoline GDT uses a 64-bit code descriptor while still entering 32 bit protected mode and its data descriptor has a
zero limit

The 10s delay is from the uncalibrated busy wait loops in the INIT/SIPI delays and AP wait. After 10s I got a success
message saying all MADT APs are active. Nahhh the delay is too long. It's really damn long.

The long delay comes from treating loop counts as time. the startup path runs ap_delay(20,000,000) plus two more 
million-iteration delays. Their duration varies under QEMU. ap_wait_online() also has a 100 million iteration
timeout. Replaced ap_delay with ap_delay_us. So it is using a PIT channel 2.

New ap_wait_online preserves the required INIT/SIPI timing while making a failed AP start report within about a 
second instead of appearing frozen for minutes
the delay calls still use the old numbers, but ap_delay_us() interprets them as microseconds (maybe this is it). Yeah
that was partly it. -smp 2 works instantly but not higher cores.

The first AP changes the shared trampoline GDT at the line that writes 0x00af9a00. The second AP reuses that GDT, so 
its protected-mode jump sees selector 0x08 as 64-bit code before long mode is enabled. That prevents the second AP 
from starting. BAM triple fault and that is confirmed by qemu.log when I do make debug with -smp 3 -m 4G because it says
triple fault check exception old : 0x8 new 0xd. 
Basically the trampoline is like a mini bootloader for the APs because they can't just jump gung ho into my higher half
C code. One AP has 0x08 as a result of its trampoline but the other(s) in protected mode see 64 bit and triple fault.

Yep that was it. It is fixed now. Changed it to 0x18.

# SLUB

Efficient for kernel objects smaller than a page. Nice to have. Free list linking slabs. Slabs are a page split into
slots for objects in a cache. kmalloc-64 cache -> 64 byte slots.

When code asks for an object:

1) SLUB looks for a free slot in the cache.
2) If one is available, it removes it from the freelist and returns it.
3) If the current slab is full, it finds another slab with free slots.
4) If there isn’t one, SLUB asks the buddy allocator for pages and splits them into slots.

After a basic allocator for SLUB with a single freelist for all CPUs with a spinlock on it, I can look at a per CPU
free list but that needs reliable per CPU state. It can come later tbh. SLUB itself is a push for a single dev.

slab->pages is virtual uint64_t. The free list is of free_obj

slab_alloc pops one slot from that slab’s freelist. Return the object pointer, or NULL if the slab is empty

cache's metadata lives inside the slab block, object size and alignment calculations need stronger bounds checks.

Currently there is only a single spinlock per cache, which is quite a bottleneck but I'd like to have at least an MVP,
I can always refactor this later.

# kmalloc

K so I kinda grouped everything into kmalloc.c, there's kfree and all that too.

16..2048 in powers of two, one slub cache each, alignment equal to the class size
Classes up to 256 use order-0 slabs, 512 and up use order-2 slabs so each slab still holds several objects after the 
slab_t header. Anything above 2048 goes straight to buddy, rounded up to a power of 2 page count
 page_meta holds one byte per physical frame, indexed by pfn. Slab objects write (class + 1) for the page they live in 
at kmalloc time. Large allocations write 0x80 | order on their first page. kfree reads that byte to pick slub_free or 
buddy_free_order. Single-byte stores keep this safe on SMP. A zero byte means unknown, and kfree ignores the pointer, 
which also makes large double frees harmless.

tbh this is all pretty much just standard code. Most linux code has similarly shaped stuff and I just had to change the
APIs. No need to reinvent the wheel ig.

spinlocks need to disable interrupts, or an interrupt that allocates while the lock is held will deadlock
single lock per cache will become a bottleneck, so per-CPU slab caches eventually ig

The important thing is the spinlock interrupt deal. That's important. Must fix that. The single lock thing is just a
temporary chokepoint that can always have a unit refactor later. no major api change or anything.

I looked it up and irq is usable to fix this. So I'm replacing the spin_unlock and lock scattered all over with irq.
spin_lock and spin_unlock became spin_lock_irqsave and spin_unlock_irqrestore. Guess it's saving context and stopping
interrupts?

irq_save: reads the CPU flags register (pushfq, then popq), then runs cli, which disables interrupts. It returns the
    old flag

irq_restore: runs sti only if the old flags had the interrupt bit (0x200) set. This is what makes nesting work. 
    When slub holds its lock and then takes the buddy lock, interrupts are already off, so the inner restore 
    leaves them off. Only the outer restore turns them back on.

spin_lock_irqsave: saves and disables interrupts, then takes the lock. Interrupts go off first, so there’s no gap where
    an interrupt could arrive after you hold the lock

spin_unlock_irqrestore: releases the lock, then restores interrupts. The order is reversed on the way out

flags travel in a local variable, which is why every exit path in buddy_alloc_order and buddy_free_order now passes 
flags back. Each unlock must restore, or interrupts stay off after the function returns, which will fuck it all up.

!! pushfq in irq_save needs the kernel built with -mno-red-zone

buddy init doesn't need a lock. it runs before the cores are even brought online.

# Memory test

327 checks, 0 failed.

## spinlock 

- lock state, interrupts off inside the irqsave section, nested save/restore, original interrupt state restored.

## buddy

- Alignment and zeroing up to order 16, with exact page accounting.
- Rejection of wrong-order, interior-page, misaligned, double and absurd-order frees.
- 256-page overlap stamping.
- Coalescing after ascending, descending and shuffled frees.
- A random mixed-order workload.
- A full exhaustion test.

## slub

- The raw slab_* functions on a static buffer.
- Five caches taken through grow, partial free, reuse and full free, checking exact slab page usage and that exactly 
    one empty slab is kept.
- An oversize cache that must fail without leaking a page.

## kmalloc

- Every size class edge, large sizes and their rounding, invalid arguments and double frees.
- kzalloc over dirty memory and krealloc grow, shrink and into-large cases.
- 20,000 random alloc/realloc/free operations checked by byte patterns, ending with a check that the buddy page count
    is back at its baseline.
