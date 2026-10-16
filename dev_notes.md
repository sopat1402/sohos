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

# LAPIC and per CPU data

PIC is just for one cpu so it has to be mapped to 0x20 and up (vectors 32+, see IDT) and masked because I need per CPU
data.

Local APIC (LAPIC): one per core. It handles that core’s own timer, inter-processor interrupts (IPIs), and acknowledging
    interrupts. It’s controlled through memory-mapped registers at a physical address (0xFEE00000 by default, and ACPI’s
    MADT gives it to us). Every core uses the same address but sees its own LAPIC

MADT parsing: the LAPIC physical address (with the override entry) and the AP list.
LAPIC enable: ap_enable_local_apic sets the enable bit and the spurious vector to 0xFF, with TPR at 0, on the BSP and on
    each AP.
A calibration reference: ap_delay_us is a PIT channel 2 one-shot that is polled through port 0x61. That’s exactly the 
    clock I need to calibrate the LAPIC timer, so it just needs to move out of ap_boot.c and become public.

This stuff is already done.

Right now the IDT only knows about the 32 CPU exceptions, and every one of them ends in a handler that prints the 
register dump and halts. That’s right for faults, but a timer tick is the opposite. the CPU should run a short handler 
and then go back to whatever it was doing. So this must make it tell those 2 apart.

## ISR and IDT change for more stubs for vector 32+

CPU looks up IDT[N], pushes SS, RSP, RFLAGS, CS, RIP + error code for exceptions, clears IF, and jumps to the gate’s 
address. The LAPIC brings up N.

I'm adding 224 more stubs for vectors 32 to 255, in the same 16-byte-spaced layout, so C can compute a stub’s address 
instead of needing 224 named symbols like for those 32. interrupt_dispatch, the single C entry point, replacing 
exception_handler as the target of isr_common -> that unfortunately means having to deal with GAS assembly again 🤮.

irq_register(vector, handler), so the timer code can later say vector 0x40 is mine.
A gap for vector 0xFF (the spurious vector already programmed into the LAPIC) with no handler registered it 
just returns, and it must not send an EOI. This stuff will be hard to add tbh.

So the interrupt frame struct is going out of idt.c and into idt.h so that functions outside can include it and take
it as a parameter. interrupt dispatch is handling the vectors 32 to 255. exception handler is for below 32. Those 32 
exception names are now in a vector. isr stub is marked and now it calls interrupt_dispatch.

- Printing from handlers: it works, but display_set_cursor and print aren’t locked, so two cores printing at once will interleave. Fine for debugging, not for anything permanent.
- Stack switching for later: all interrupts currently run on whatever stack was active. That’s fine for kernel-only code.
    When i add user mode, you’ll need a TSS rsp0 so an interrupt from ring 3 lands on a kernel stack. The IST field on 
    each gate (still 0) is also where the double fault stack goes.
- The AP entry: ap_kernel_entry already calls idt_load(), so the APs pick up the new gates automatically

so, this is calculating an interrupt address but I don't want the rest of my functions to have to worry about poking
raw addresses. that's why I'll wrap them in lapic.c

## lapic.c

it will read and write registers, ap_enable_local_apic moves here to enable lapic. also on EOI (end of interrupt),
0 is written to the register to say the handler is done and to accept a new one. if this isn't done lapic will assume
it is busy and not send another. it also needs a countdown timer before firing a vector. it'll be given a number and a
vector. but omfg the timer must be calibrated first because it has to sync with the hardware clock. must use PIT to count
10ms and then count ticks per millisecond. ap_lapic_send_ipi will come in here.

Other functions should not know the EOI registers and register offsets and MMIO.

Without a timer interrupt, a thread runs until it chooses to give up the CPU. With one, the kernel gets control back 
every few milliseconds whether the thread wants it or not, and it can switch to another thread. Ik the value of 
interrupts because I wrote an async runtime in C and it was cooperative :(

The lapic timer vector is 0x40. So, lapic.c is just EOI + timer + calibration plus the code moved from ap_boot.c legacy
is just for pit delay and disable. have used pit delay in ap boot, it was the reason my boot was taking long because
delay_us was taking microseconds but i gave a much bigger number. pit_delay_us is literally just ap_delay_us

pic_disable remaps the two legacy 8259 controllers to vectors 0x20 to 0x2F and then masks every line
The remap matters even though everything is masked bc a spurious PIC interrupt would otherwise arrive on vectors 8 to 15,
which are CPU exception vectors. should be called on bsp before first sti.

establish the IDT first, then disable the legacy PIC so it stops delivering its legacy IRQs. I'm moving to APIC based
handling.

check_exception old: 0x8 new 0xe
Triple fault

ugh got this as a triple fault when I try to get the BSP to disable interrupts and start its timer. Exception 8 was the
error (double fault) but exception 14 made the cpu give up. qemu log says it's around a gdt address so ig. cs is loaded
from gdt so first tick after sti panics. GDT load now calls CS with a high return. gdt.c leaves fs and gs alone because
the cpu pointer goes into gs. worked and it passes mem_test too

## per CPU data

One struct percpu per core, allocated once at boot. Every core needs a cheap way to find its own struct from any code, 
including interrupt handlers.

x86-64 has a segment register, GS, whose base address can be set per core through a model-specific register 
(IA32_GS_BASE, MSR 0xC0000101). Each core has its own copy of that MSR. so, each cpu can set its gs base to its 
struct percpu and its first field is a pointer to itself. so, mov %gs:0, reg reads the address and returns it. that's
what percpu_self() does. no lock or anything needed. gives the struct addr just like that.
the struct has cpu id, apic id and tick count, which replaces the temp array in lapic.c

If the BSP’s percpu_attach comes after the timer starts, the first tick reads gs:0 with a zero base, which is a page 
fault at address 0

one 64 byte aligned struct per core. The BSP is cpu_id 0 and the APs follow in boot plan order. percpu_attach(apic_id) 
writes the matching struct address to IA32_GS_BASE.
tick handler now increments the running core’s own counter. The temporary array is gone, and lapic_timer_ticks looks a 
core up by APIC ID, so the print loop works unchanged.each AP calls percpu_attach(lapic_id()) after its LAPIC is enabled
and before it counts itself online.

heap init is moved earlier because the structs need to be allocated.

# Threads and scheduling

So, a thread is just a saved execution state and the memory it needs to resume. register state + stack + bookkeeping

until user mode, x87/SSE/AV state is not touched as it is booted with general regs only as of now.

- The general-purpose registers.
- The instruction pointer (rip) and stack pointer (rsp).
- rflags.
- The segment registers, which are constant for kernel threads (ignored for now).
- CR3, if the address space differs.
- FS and GS bases, if threads have thread-local storage

Context switching isn't too hard because when I wrote my async runtime, I first made it stackful and then stackless.
whichever one needs registers saved.

if a thread calls schedule, SYSTEM VABI says it is supposed to know that rax,rax,rdx,r8-r11,rsi,rdi are destroyed.
callee saved things must be saved : rbx, rbp, r12, r13, r14, r15, plus rsp. Call already pushed return address to the
stack. (cooperative)
A new thread that has never run anything has no context so spoof it and make it look like a thread that was switched out
clever stuff rlly (stack must be alloced too).

for a first version, thread control block is just one struct per thread.

If the handler calls schedule() before the EOI, and the switch goes to a thread that doesn’t return to this core’s 
handler for a long time, the LAPIC never gets its EOI and no more timer ticks arrive on that core. so, EOI should be
sent before schedule.

a trampoline is needed (waahhh again) in the context switch. it needs to enable interrupts, release any scheduler lock,
move r13 into rdi and call r12, call thread_exit when the function returns.

spinlocks are not enough. need a wait queue for mutexes and symaphores.
initially new threads go to the least loaded core, and an idle core steals if it finds its own queue empty. affinity
(locking thread to core) can be added later.

Round robin scheduling first. Everything above builds on the same switch and queue machinery. Plan is CFS style red
black tree and a vruntime for each.

The scheduler: one run queue per core, round robin, preemptive.
Run queue: an intrusive FIFO list guarded by a per-queue spinlock that is only ever taken with interrupts off.
No code holds two run queue locks at once, so there is no lock ordering to get wrong. Threads are bound to the core
they are first enqueued on, so only that core ever picks them. There is no migration or stealing yet.
schedule(): disables interrupts, puts the running thread at the tail if it is still runnable (the idle thread is never
queued), pops the head or falls back to the core's idle thread, and switches. If the pick is the same thread it just
refreshes its slice and returns. The queue lock is dropped before switch_to. The thread that resumes runs
sched_finish_switch for the thread it replaced, which clears that thread's on_cpu flag and frees it if it was a
zombie. A new thread runs the same step from thread_trampoline. Because the finish step runs only after the old
stack has been left, a zombie's stack is never freed while in use.
sched_tick runs from the timer handler after the EOI, so the switch it triggers cannot block further ticks. It
counts the running thread's slice down and calls schedule when the slice is gone and something else is ready,
or when the core is idle and work has arrived.
Idle: every core's boot context is adopted by sched_adopt_boot_thread as that core's idle thread. The idle loop
checks the queue with interrupts off and then uses sti followed directly by hlt. sti takes effect after the next
instruction, so an interrupt cannot slip in between and leave the core halted with work waiting.
Wakeups across cores: sched_enqueue on another core sends it SCHED_IPI_VECTOR, whose handler reschedules only if the
target is idle with work waiting.
Placement: sched_pick_cpu picks the online core with the smallest ready count plus one if it is running a non-idle
thread, starting from a rotating cursor so ties spread out.
no percpu pointer across anything that can reschedule once threads can migrate. schedule() and sched_tick keep theirs 
only because nothing migrates yet.

To integrate, I should init the scheduler before ap_start and save the CPUs boot context as its idle thread.

ap_kernel_entry is already adopting the thread.

thread_create(name, fn, arg) chooses a CPU and enqueues the thread.
thread_create_on(name, fn, arg, cpu) lets you choose a particular CPU.
sched_idle_loop() lets the BSP idle while waiting for work

thread_fn is a function that returns void and takes a void* arg.

it passes the thread_create. 3 threads made. Made 3 counters and a function to increment. Had to keep as volatile else
gcc optimises them out and i can't see them in gdb. make debug now makes a server that gdb links to with make gdb.

CPU	    Before	        After 10 s	    Increase
CPU0	452,511,099	    1,459,045,789	+1,006,534,690
CPU1	416,862,633	    1,726,237,751	+1,309,375,118
CPU2	545,354,577	    1,278,932,723	+733,578,146

the threads are therefore, doing their work.

# Page table walking

Need to have a virtual addr and find which page has it. bits 47 to 39 give pml4, 38 to 30 give pdpt, 29 to 21 give pd
and 20 to 12 give pt. then add offset virt_addr & 0x1FF. I am masking flags with 0x000FFFFFFFFFF000ULL.
But each time I need to do phys_to_virt and then index the address for the entry and then mask the flags.

pdpt_entry & (1ULL << 7) means 1 GiB mapping if it is true. for pd it is 2 MiB page.

in a 1 GiB mapping, a 30 bit offset is present in virt addr:
uint64_t phys_addr = (pdpt_entry & 0x000FFFFFC0000000ULL) | (virt_addr & 0x3FFFFFFFULL)

for 2 MiB, uint64_t phys_addr = (pd_entry & 0x000FFFFFFFE00000ULL) | (virt_addr & 0x1FFFFFULL)

now need to make a vm_map to add pages if needed.

If the virtual address is already mapped, it returns the existing physical address.
If one or more intermediate tables are missing, it allocates only those tables, plus one physical page for the mapping.
If allocation fails, it frees every page allocated during the failed attempt. It doesn't leave partially installed 
page-table entries behind.
New mappings are supervisor-only, writable and non-executable.
Existing huge-page mappings are left alone rather than accidentally being interpreted as pointers to smaller tables

ordinary kernel page:
uint64_t phys;
enum vm_map_status status = vm_map(root_phys, virt_addr, VM_READ | VM_WRITE, &phys);

for a user-readable, user-writable page:
uint64_t phys;
enum vm_map_status status = vm_map(root_phys, virt_addr, VM_READ | VM_WRITE | VM_USER, &phys);

for a read-only executable page:
uint64_t phys;
enum vm_map_status status = vm_map(root_phys, virt_addr, VM_READ | VM_EXEC, &phys);

next is vm_unmap to clear the page table entry. used when a process exits and when it is no longer needed.

to call,
uint64_t unmapped_phys;

enum vm_unmap_status status = vm_unmap(root_phys, virt_addr, &unmapped_phys);

if (status == VM_UNMAP_OK) {
    buddy_free_order(unmapped_phys, 0);
}

Local invalidation only.

- Removes a 4 KiB mapping and returns its physical frame.
- Does not free the frame or intermediate page tables.
- Caller is responsible for frame ownership and ensuring stale TLB translations cannot access a reclaimed frame.

so, it returns out_phys but doesn't free it.

!! vm_map allocs the page on its own and maps 1 4 KiB page per call.

# TLB

already have vm_invlpg, can flush for one specific page.
I already have wakes for other cores but added a tbl_ack_generation to the percpu struct.

mark a CPU online only after its per-CPU attachment, IDT, and local APIC are ready. I'll keep the 
online CPU set fixed once normal VM operations begin. no CPU hotplug yet for an initial version. Don't need a production
grade TLB right off the bat. It's a cache ffs.

An interrupt could preempt the code between writing ICR_HIGH and ICR_LOW, and another sender could overwrite the 
destination. that register pair needs protection by disabling local interrupts around it. lapic_send_ipi is changed for
that.

for now it is broadcasting to all CPUs and flushes all local translations on all CPUs. inefficient but functional.
I'll refactor it once I can actually do stuff with this thing.

mf cpu is like : I have a perfectly good translation cached. Why would I check your table again? i miss when cache meant
a shitty little doubly linked list and hash map. Here it's not enough to change my kernel mapping. I must call invlpg
and make the tlb cache of the cpu itself clear.

I added tickets and interrupts should be kept on by the waiters.

# vmalloc

kernel image starts at 0xFFFFFFFF80000000, while the HHDM starts at 0xFFFF800000000000. I need to find a place to put
my vmalloc region. after that I need a vmalloc allocator and then it's straightforward. Just round up the bytes requested
to pages and alloc the pages and map them.

vmalloc	0xFFFFC00000000000 – 0xFFFFC10000000000 gives a 1 TiB virtual address window. Also aligned to pml4 boundaries.

I made a struct for an alloctor and a linked link for mem regions. Not considering fragmentation yet.

ugh need to use kmalloc in vmalloc for a linked list it's a linked list so it needs a heap. fuck me. my memory allocator
depends on my memory allocator.

I'm currently using the cr3 as the root phys given to vm_map in vmalloc.

If kmalloc() fails during rollback, ts leaks the virtual reservation. A proper implementation should preserve a 
reservation descriptor so rollback never depends on a fresh allocation. rollback is on failure btw. The tlb global
broadcast and total flush is just painful ATP. but it can be optimised later. it is functional rn.

and if their base+size is not on a page boundary but there's actual data after? suppose they alloc 4094, rounded to 
4096 but free 4090 but have an aligned base. 4 bytes get fucked. fuck. so it's not enough for aligned base and size.
I must therefore have allocation metadata so that I can just do vfree(ptr) and let it check the data and figure it tf
out.

cr3 is used because I don't have address spaces yet 😭 also there's a chance my rollback loses track of mapped pages.
buddy_free_order error is potentially ignored too. free_space is also not checked yet. just trusted merging.

vfree gives 0 on success, -1 on failure. key part here is, I'm using the metadata's size from the base that the caller
gives.

I added a quarantine list to the rollback process. TBH at this point even if there is an unclaimed page bug idgaf. I can
debug it later. It is strapped to the teeth with methods (at least as far as I care to do rn) for rollbacks.

I just hope I don't end up with a dependency loop where kmalloc tries to use vmalloc eventually but vmalloc uses kmalloc.

btw I'm not testing rn but a caller need to ensure that vmalloc_init has been called else the allocator head will not
point to the initial struct.

# Per core TSS and per core GDT

CPU needs to be able to switch stacks. Useful when a ring 3 program (userspace) makes an interrupt. It needs to switch
to run on the kernel stack and it can't use a user controlled stack.

RSP0 is what it will use. It's the stack pointer. To make it portable btw, I will be keeping only ring 0 and 3.
Need to make cpu read TSS.RSP0. Each thread needs its own kernel stack. RSP0 must be updated depending on which thread
is running.

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

need to add it to each cpu, kmalloc is initialised before. I'll just embed it into the per cpu struct ig. i will give 
each CPU its own GDT in the per cpu struct too. maybe just pointers idk.
meh I'm not using pointers for it because per cpu is calling kzalloc anyways.

CPU marks a TSS descriptor busy when loaded with LTR

I made the per cpu struct have the gdt and tss struct in it. Now I can reuse most of the gdt load. I must pass the
per cpu struct and then gdt[0..4] needs to get null, kernel and user descriptors. then, p->tss address goes into
gdt[5] and gdt[6]. then I built a gdt_pointer struct with base as p->gdt and limit as sizeof(p->gdt)-1.
then the same lgdt and cs reload sequence. i then finally execute ltr using the tss sequence 0x28.
