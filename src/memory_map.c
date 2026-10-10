#include <stdint.h>
#include "../include/memory_regions.h"

#define FRAME_SIZE 4096ull

static uint64_t align_up(uint64_t x, uint64_t a){
    return (x+a-1)&~(a-1);
}

static uint64_t align_down(uint64_t x, uint64_t a){
    return x&~(a-1);
}

static int ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end){
    return a_start<b_end && b_start<a_end;
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

void mark_free_memory(uint64_t kstart, uint64_t kend, uint64_t bitmap_start, uint64_t bitmap_end, uint64_t info_start, uint64_t info_end){
    if (bitmap_end<=bitmap_start)
        return;

    uint8_t *bitmap=(uint8_t *)(uintptr_t)bitmap_start;
    uint64_t nframes=(bitmap_end-bitmap_start)*8;

    for (uint32_t r=0;r<memory_region_count();r++){
        const struct mem_region *region=memory_region_get(r);
        frames_mark_range(bitmap,nframes,region->base/FRAME_SIZE,region->end/FRAME_SIZE,0);
    }

    reserve_range(bitmap,nframes,kstart,kend);
    reserve_range(bitmap,nframes,bitmap_start,bitmap_end);
    reserve_range(bitmap,nframes,info_start,info_end);
}

void mark_frame(uint8_t *bitmap_start, uint64_t frame, int value){
    uint64_t byte=frame / 8;
    uint8_t bit=frame % 8;
    uint8_t *byte_addr=(uint8_t *)bitmap_start+byte;
    uint8_t val=*byte_addr;
    if (value){
        val |= (uint8_t)(1u << bit);
    }else{
        val &= (uint8_t)~(1u << bit);
    }
    *byte_addr=val;
}

uint64_t alloc_frame_basic(uint8_t *bitmap_start, uint8_t *bitmap_end){
    uint64_t nframes=(uint64_t)(bitmap_end-bitmap_start)*8;
    uint64_t limit=(uint64_t)262144/8;
    if (nframes<262144) limit=nframes/8;
    uint64_t address=0;
    for (uint8_t *byte=bitmap_start;byte<bitmap_start+limit;byte++){
        if (*byte==255){
            address+=8*FRAME_SIZE;
            continue;
        }
        for (int bit=0;bit<8;bit++){
            if ((*byte & (1u << bit))==0){
                *byte|=(uint8_t)(1u<<bit);
                uint64_t *frame=(uint64_t *)address;
                for (int i=0;i<512;i++)
                    frame[i]=0;
                return address;
            }
            address+=FRAME_SIZE;
        }
    }
    return 0;
}

uint64_t count_free_frames(uint8_t *bitmap_start, uint8_t *bitmap_end){
    uint64_t count=0;
    for (uint8_t *byte=bitmap_start;byte<bitmap_end;byte++){
        if (*byte==0xFF){
            continue;
        }
        for (int bit=0;bit<8;bit++){
            if ((*byte & (1u<<bit))==0){
                count++;
            }
        }
    }
    return count;
}

uint64_t alloc_frame_in_range(uint8_t *bitmap_start, uint8_t *bitmap_end, uint64_t first_frame, uint64_t end_frame){
    if (bitmap_start == 0 || bitmap_end <= bitmap_start || end_frame <= first_frame)
        return 0;

    uint64_t bitmap_bits = (uint64_t)(bitmap_end - bitmap_start) * 8;
    if (end_frame > bitmap_bits)
        end_frame = bitmap_bits;

    for (uint64_t frame = first_frame; frame < end_frame; frame++) {
        uint8_t mask = (uint8_t)(1u << (frame % 8));
        uint8_t *byte = &bitmap_start[frame / 8];

        if ((*byte & mask) == 0) {
            *byte |= mask;

            uint64_t address = frame * FRAME_SIZE;
            uint64_t *words = (uint64_t *)(uintptr_t)address;

            for (uint64_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++)
                words[i] = 0;

            return address;
        }
    }

    return 0;
}
