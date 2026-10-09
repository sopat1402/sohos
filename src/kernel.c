#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/display.h"

uint64_t frame_size = 4096;

extern char kernel_start;
extern char kernel_end;

static inline void write_cr3(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

void kmain(uint32_t magic, uint64_t multiboot_data){
    if (magic!=0x36d76289){
        return;
    }
    //bitmap construction
    uint64_t max_addr=max_available_memory_address(multiboot_data);
    uint64_t max_frames=max_addr/(uint64_t)frame_size;
    uint64_t num_bytes=((max_frames+7)/8);
    uintptr_t kstart=(uintptr_t)&kernel_start;
    uintptr_t kend=(uintptr_t)&kernel_end;
    struct multiboot_mmap_entry bitmap_region=bitmap_space(num_bytes,multiboot_data,kstart,kend);
    for (uint64_t i=bitmap_region.base_addr;i<bitmap_region.base_addr+bitmap_region.length;i++){
        uint8_t *base_addr=(uint8_t *)i;
        *base_addr=0xFF;
    }
    mark_free_memory(
        multiboot_data,
        kstart,
        kend,
        (uintptr_t)bitmap_region.base_addr,
        (uintptr_t)(bitmap_region.base_addr+bitmap_region.length)
    );
    uint8_t *bitmap_start=(uint8_t *)bitmap_region.base_addr;
    uint8_t *bitmap_end=bitmap_region.length+bitmap_start;
    print("Kernel start : ");
    print_uint(kstart);
    print("\n");
    print("Kernel end : ");
    print_uint(kend);
    print("\n");
    print("Bitmap region start : ");
    print_uint((uint64_t) bitmap_start);
    print("\n");
    print("Bitmap region end : ");
    print_uint((uint64_t)bitmap_end);
    print("\n");

    //new tree
    print("Total free frames : ");
    uint64_t free_frames=count_free_frames(bitmap_start,bitmap_end);
    print_uint(free_frames);
    print("\n");
    uint64_t *pml4=(uint64_t *)alloc_frame(bitmap_start,bitmap_end);
    uint64_t *pdpt=(uint64_t *)alloc_frame(bitmap_start,bitmap_end);
    uint64_t *pd=(uint64_t *)alloc_frame(bitmap_start,bitmap_end);
    pml4[0]=(uint64_t)pdpt | 0x3;
    pdpt[0]=(uint64_t)pd | 0x3;
    for (int i=0;i<512;i++){
        uint64_t base_addr=(uint64_t)i*0x200000;
        uint64_t entry_val=base_addr | 0x83;
        pd[i]=entry_val;
    }
    write_cr3((uint64_t)pml4);
    //test if triple fault or not
    print("Total free frames : ");
    free_frames=count_free_frames(bitmap_start,bitmap_end);
    print_uint(free_frames);
    print("\n");

}
