#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/display.h"

uint64_t frame_size = 4096;

extern char kernel_start;
extern char kernel_end;

void kmain(uint32_t magic, uint64_t multiboot_data){
    if (magic!=0x36d76289){
        return;
    }
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
    print("Kernel start : ");
    print_uint(kstart);
    print("\n");
    print("Kernel end : ");
    print_uint(kend);
    print("\n");
    print("Bitmap region start : ");
    print_uint(bitmap_region.base_addr);
    print("\n");
    print("Bitmap region end : ");
    print_uint(bitmap_region.base_addr+bitmap_region.length);
    print("\n");
}
