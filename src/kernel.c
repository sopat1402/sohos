#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/memory_regions.h"
#include "../include/display.h"
#include "../include/paging.h"
#include "../include/idt.h"
#include "../include/phys.h"
#include "../include/buddy.h"
#include "../include/ap_boot.h"
#include "../include/kmalloc.h"
#include "../include/legacy.h"
#include "../include/lapic.h"
#include "../include/gdt.h"
#include "../include/percpu.h"
#include "../include/sched.h"
#include "../include/thread.h"
#include "../include/vmalloc.h"
#include "../include/address_space.h"
#include "../include/syscall.h"
#include "../include/usercpy.h"
#include "../include/tlb.h"
#include "../include/ps2.h"
#include "../include/mb2_module.h"
#include "../include/initrd.h"
#include "../include/pci.h"

#define PAGE_SIZE 4096ull
#define BOOTSTRAP_LIMIT 0x100000000ULL

extern char __image_phys_start;
extern char __kernel_phys_start;
extern char __kernel_phys_end;
extern char __kernel_virt_start;
extern char __kernel_virt_end;

extern char kernel_stack_bottom, kernel_stack_top;

static struct rsdp_v2 boot_rsdp;
static uint8_t boot_rsdp_revision;
static struct cpu_preapic_info bsp_cpu;
static struct ap_boot_plan ap_plan;
static int ap_plan_ready;

void kmain(uint32_t magic, uint64_t multiboot_data) {
    idt_init();
    pic_disable();
    print("Welcome to SOHOS!\n");
    if (magic != 0x36d76289) {
        print("[error] Invalid Multiboot magic\n");
        return;
    }

    if (multiboot_data == 0 || multiboot_data >= BOOTSTRAP_LIMIT || BOOTSTRAP_LIMIT - multiboot_data < sizeof(uint32_t)) {
        print("[error] Multiboot info is outside the bootstrap map\n");
        return;
    }

    uint32_t multiboot_info_size = *(const uint32_t *)phys_to_virt(multiboot_data);
    if (multiboot_info_size < 16 || multiboot_info_size > BOOTSTRAP_LIMIT - multiboot_data) {
        print("[error] Invalid Multiboot info size\n");
        return;
    }


    //read acpi and apic
    const void *mbi = phys_to_virt(multiboot_data);
    int rsdp_status = mb2_find_acpi_rsdp(
        mbi,
        (uint32_t)(BOOTSTRAP_LIMIT - multiboot_data),
        &boot_rsdp,
        &boot_rsdp_revision
    );
    if (rsdp_status == 1) {
        kprintf("ACPI RSDP revision: %d\n",boot_rsdp_revision);
    } else if (rsdp_status == 0) {
        print("No ACPI RSDP tag in Multiboot info\n");
    } else {
        print("Malformed Multiboot ACPI RSDP tag\n");
    }
    if (!cpu_probe_preapic(&bsp_cpu)) {
        print("CPUID probe failed\n");
        return;
    }
    if (!bsp_cpu.has_apic)
        print("CPU reports no Local APIC support\n");
    if (bsp_cpu.has_apic && !bsp_cpu.lapic_enabled)
        print("Local APIC is currently disabled\n");
    if (rsdp_status == 1 && bsp_cpu.has_apic && bsp_cpu.has_msr) {
        int ap_status = ap_prepare_early(
            mbi,
            multiboot_data,
            multiboot_info_size,
            &boot_rsdp,
            boot_rsdp_revision,
            bsp_cpu.bsp_apic_id,
            &ap_plan
        );

        if (ap_status < 0) {
            print("[error] AP preparation failed\n");
            return;
        }

        ap_plan_ready = (ap_status == 1);

        if (ap_plan_ready) {
            kprintf("Prepared APs: %d\n",ap_plan.ap_count);
        } else {
            print("MADT has no secondary APs to start\n");
        }
    } else {
        print("AP startup unavailable: missing ACPI or APIC support\n");
    }

    //init of initrd
    uint64_t initrd_start = 0;
    uint64_t initrd_end = 0;
    int initrd_status = mb2_find_module(phys_to_virt(multiboot_data), (uint32_t)(BOOTSTRAP_LIMIT - multiboot_data), &initrd_start, &initrd_end);
    if (initrd_status < 0) {
        print("[error] Malformed Multiboot module tag\n");
        return;
    }

    //define physical start regions of the image
    uint64_t image_phys_start = (uint64_t)(uintptr_t)&__image_phys_start;
    uint64_t image_phys_end   = (uint64_t)(uintptr_t)&__kernel_phys_end;

    uint64_t kernel_phys_start = (uint64_t)(uintptr_t)&__kernel_phys_start;
    uint64_t kernel_phys_end   = (uint64_t)(uintptr_t)&__kernel_phys_end;
    uint64_t kernel_virt_start = (uint64_t)(uintptr_t)&__kernel_virt_start;
    uint64_t kernel_virt_end   = (uint64_t)(uintptr_t)&__kernel_virt_end;

    if (image_phys_start >= image_phys_end ||
        image_phys_end > BOOTSTRAP_LIMIT ||
        kernel_phys_end <= kernel_phys_start ||
        kernel_virt_end <= kernel_virt_start ||
        kernel_phys_end - kernel_phys_start !=
            kernel_virt_end - kernel_virt_start) {
        print("[error] Invalid kernel load or virtual range\n");
        return;
    }
    if (kernel_phys_start >= kernel_phys_end || kernel_phys_end > BOOTSTRAP_LIMIT) {
        print("[error] Kernel is outside the bootstrap map\n");
        return;
    }

    //parse multiboot data into a memory map
    if (!memory_regions_init(multiboot_data)) {
        print("[error] No available memory reported\n");
        return;
    }

    uint64_t total_usable_bytes = memory_regions_total_bytes();
    uint64_t highest_usable_end = memory_regions_highest_end();

    print("Total usable RAM: ");
    print_size(total_usable_bytes);
    print(" (");
    print_uint(total_usable_bytes);
    print(" bytes)\n");

    uint64_t max_frames = highest_usable_end / PAGE_SIZE;
    if (highest_usable_end % PAGE_SIZE != 0)
        max_frames++;

    uint64_t bitmap_bytes = max_frames / 8;
    if (max_frames % 8 != 0)
        bitmap_bytes++;

    if (bitmap_bytes == 0) {
        print("[error] Bitmap size is zero\n");
        return;
    }

    uint64_t info_start = multiboot_data;
    uint64_t info_end = multiboot_data + multiboot_info_size;

    struct mem_region bitmap_region = bitmap_space(bitmap_bytes, image_phys_start, image_phys_end, info_start, info_end,initrd_start,initrd_end);

    if (bitmap_region.end <= bitmap_region.base || bitmap_region.end > BOOTSTRAP_LIMIT) {
        print("[error] Bitmap is outside the bootstrap map or invalid\n");
        return;
    }

    //initial bitmap for making a new tree and stack
    bitmap_init(bitmap_region.base, bitmap_region.end - bitmap_region.base);
    mark_free_memory(image_phys_start, image_phys_end, info_start, info_end,initrd_start,initrd_end);

    //make new tree and new stack
    uint64_t lapic_map_phys = ap_plan_ready ? ap_plan.lapic_phys : (bsp_cpu.has_apic ? bsp_cpu.lapic_phys_base : 0);
    if (!new_tree(kernel_phys_start, kernel_phys_end, kernel_virt_start, kernel_virt_end, ap_plan_ready ? ap_plan.trampoline_phys : 0, lapic_map_phys)) {
        print("[error] Page-table setup failed\n");
        return;
    }

    //set up bootstrap gdt
    gdt_load();

    //initialise buddy allocator
    if (!buddy_init(bitmap_region.base,bitmap_region.end - bitmap_region.base)) {
        print("[error] Buddy allocator initialization failed\n");
        return;
    }

    //initialise the heap built on slub
    if (!kmalloc_init()) {
        print("[error] kmalloc initialization failed\n");
        return;
    }

    pci_init();
    pci_dump();

    //init of initrd
    if (initrd_status == 1) {
        int files = initrd_init(initrd_start, initrd_end);
        print("initrd files: ");
        print_uint(files < 0 ? 0 : (uint64_t)files);
        print("\n");
    }

    //set up lapic and timer
    if (!lapic_init(lapic_map_phys) || !lapic_enable()) {
        print("[error] Local APIC setup failed\n");
        return;
    }
    lapic_enable();
    if (!lapic_timer_calibrate()) {
        print("[error] LAPIC timer calibration failed\n");
        return;
    }
    if (!lapic_timer_install()) {
        print("[error] LAPIC timer handler install failed\n");
        return;
    }

    //kzalloc and assign per cpu structs
    if (!percpu_init(bsp_cpu.bsp_apic_id, ap_plan_ready ? ap_plan.apic_ids : 0, ap_plan_ready ? ap_plan.ap_count : 0)) {
        print("[error] per-CPU setup failed\n");
        return;
    }
    if (!percpu_attach(bsp_cpu.bsp_apic_id)) {
        print("[error] BSP per-CPU attach failed\n");
        return;
    }
    if (!tlb_init()) {
        for (;;)
            __asm__ volatile ("cli\n\thlt");
    }
    //set tss and gdt
    percpu_gdt_setup(percpu_self());
    struct percpu *p = percpu_self();
    p->boot_stack_base = &kernel_stack_bottom;
    p->boot_stack_size = (uintptr_t)&kernel_stack_top - (uintptr_t)&kernel_stack_bottom;
    //initialise kernel_as on the BSP
    kernel_as_init();

    //boot scheduler and adopt thread
    if (!sched_init()) {
        print("[error] Failed to initialise scheduler");
        return;
    }
    if (!sched_adopt_boot_thread()) {
        print("[error] Failed to adopt thread on BSP");
        return;
    }
    //set current thread to rsp0
    set_tss_rsp0_currthrd();

    //allocate IST
    setup_ist(percpu_self());

    //initialise vmalloc
    vmalloc_init();

    //Start APs
    if (ap_plan_ready) {
        if (!ap_start_all(&ap_plan)) {
            print("[error] AP startup failed\n");
            return;
        }
    }
    cpu_fix_cr0();
    //enable syscalls
    syscall_init();

    //enable SMEP and SMAP
    enable_smep_smap();

    //Start LAPIC timer on BSP
    if (!lapic_timer_start(LAPIC_TICK_HZ)) {
        print("[error] BSP timer start failed\n");
        return;
    }

    //execute sti
    __asm__ volatile("sti" ::: "memory");

    //initialise the keyboard
    kbd_init();

    //launch shell
    const struct initrd_file *init_file = initrd_lookup("/bin/sh");
    if (init_file != NULL)
        process_create("sh", init_file->data, init_file->size);

    //sched loop
    sched_idle_loop();
}
