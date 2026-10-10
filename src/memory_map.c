#include <stdint.h>
#include "../include/memory_map.h"
#include "../include/phys.h"

#define FRAME_SIZE 4096ull
#define BOOT_FRAME_LIMIT 262144ull

static uint64_t bitmap_phys=0;
static uint64_t bitmap_size=0;

static uint64_t align_up(uint64_t x, uint64_t a){
    return (x+a-1)&~(a-1);
}

static uint64_t align_down(uint64_t x, uint64_t a){
    return x&~(a-1);
}

static int ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end){
    return a_start<b_end && b_start<a_end;
}

static uint8_t *bitmap_ptr(void){
    return (uint8_t *)phys_to_virt(bitmap_phys);
}

static void zero_frame(uint64_t phys){
    uint64_t *words=(uint64_t *)phys_to_virt(phys);
    for (uint64_t i=0;i<FRAME_SIZE/sizeof(uint64_t);i++)
        words[i]=0;
}

struct mem_region bitmap_space(uint64_t num_bytes, uint64_t kstart, uint64_t kend, uint64_t info_start, uint64_t info_end){
    struct mem_region none={0,0};
    num_bytes=align_up(num_bytes,FRAME_SIZE);

    uint64_t k_start=align_down(kstart,FRAME_SIZE);
    uint64_t k_end=align_up(kend,FRAME_SIZE);
    uint64_t i_start=align_down(info_start,FRAME_SIZE);
    uint64_t i_end=align_up(info_end,FRAME_SIZE);

    for (uint32_t r=0;r<memory_region_count();r++){
        const struct mem_region *region=memory_region_get(r);
        uint64_t candidate=region->base;
        int moved;
        do {
            moved=0;
            if (ranges_overlap(candidate,candidate+num_bytes,k_start,k_end)){
                candidate=k_end;
                moved=1;
            }
            if (ranges_overlap(candidate,candidate+num_bytes,i_start,i_end)){
                candidate=i_end;
                moved=1;
            }
        } while (moved);

        if (candidate+num_bytes<=region->end){
            struct mem_region found={candidate,candidate+num_bytes};
            return found;
        }
    }
    return none;
}

static void frames_mark_range(uint8_t *bitmap, uint64_t nframes, uint64_t first, uint64_t last, int used){
    if (last>nframes)
        last=nframes;
    for (uint64_t f=first;f<last;f++){
        uint8_t mask=(uint8_t)(1u<<(f%8));
        if (used)
            bitmap[f/8]|=mask;
        else
            bitmap[f/8]&=(uint8_t)~mask;
    }
}

static void reserve_range(uint8_t *bitmap, uint64_t nframes, uint64_t start, uint64_t end){
    if (end<=start)
        return;
    frames_mark_range(bitmap,nframes,start/FRAME_SIZE,(end+FRAME_SIZE-1)/FRAME_SIZE,1);
}

void bitmap_init(uint64_t base_phys, uint64_t bytes){
    bitmap_phys=base_phys;
    bitmap_size=bytes;
    uint8_t *bitmap=bitmap_ptr();
    for (uint64_t i=0;i<bitmap_size;i++)
        bitmap[i]=0xFF;
}

void mark_free_memory(uint64_t kstart, uint64_t kend, uint64_t info_start, uint64_t info_end){
    if (bitmap_size==0)
        return;

    uint8_t *bitmap=bitmap_ptr();
    uint64_t nframes=bitmap_size*8;

    for (uint32_t r=0;r<memory_region_count();r++){
        const struct mem_region *region=memory_region_get(r);
        frames_mark_range(bitmap,nframes,region->base/FRAME_SIZE,region->end/FRAME_SIZE,0);
    }

    reserve_range(bitmap,nframes,kstart,kend);
    reserve_range(bitmap,nframes,bitmap_phys,bitmap_phys+bitmap_size);
    reserve_range(bitmap,nframes,info_start,info_end);
}

uint64_t bitmap_frame_count(void){
    return bitmap_size*8;
}

int frame_is_free(uint64_t frame){
    if (frame>=bitmap_size*8)
        return 0;
    return (bitmap_ptr()[frame/8]&(1u<<(frame%8)))==0;
}

void mark_frame(uint64_t frame, int value){
    if (frame>=bitmap_size*8)
        return;
    frames_mark_range(bitmap_ptr(),bitmap_size*8,frame,frame+1,value);
}

uint64_t alloc_frame_basic(void){
    uint64_t limit_frames=BOOT_FRAME_LIMIT;
    if (bitmap_size*8<limit_frames)
        limit_frames=bitmap_size*8;

    uint8_t *bitmap=bitmap_ptr();
    uint64_t address=0;
    for (uint64_t byte=0;byte<limit_frames/8;byte++){
        if (bitmap[byte]==0xFF){
            address+=8*FRAME_SIZE;
            continue;
        }
        for (int bit=0;bit<8;bit++){
            if ((bitmap[byte]&(1u<<bit))==0){
                bitmap[byte]|=(uint8_t)(1u<<bit);
                zero_frame(address);
                return address;
            }
            address+=FRAME_SIZE;
        }
    }
    return 0;
}

uint64_t alloc_frame_in_range(uint64_t first_frame, uint64_t end_frame){
    uint64_t nframes=bitmap_size*8;
    if (nframes==0 || end_frame<=first_frame)
        return 0;
    if (end_frame>nframes)
        end_frame=nframes;

    uint8_t *bitmap=bitmap_ptr();
    for (uint64_t frame=first_frame;frame<end_frame;frame++){
        uint8_t mask=(uint8_t)(1u<<(frame%8));
        if ((bitmap[frame/8]&mask)==0){
            bitmap[frame/8]|=mask;
            uint64_t address=frame*FRAME_SIZE;
            zero_frame(address);
            return address;
        }
    }
    return 0;
}

uint64_t count_free_frames(void){
    uint8_t *bitmap=bitmap_ptr();
    uint64_t count=0;
    for (uint64_t i=0;i<bitmap_size;i++){
        if (bitmap[i]==0xFF)
            continue;
        for (int bit=0;bit<8;bit++){
            if ((bitmap[i]&(1u<<bit))==0)
                count++;
        }
    }
    return count;
}
