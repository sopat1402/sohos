#include <stdint.h>

#define FRAME_SIZE 4096ull
#define LOW_MEMORY_LIMIT 0x100000ull

struct multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_mmap_tag {
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
};

struct multiboot_mmap_entry {
	uint64_t base_addr;
	uint64_t length;
	uint32_t type;
	uint32_t reserved;
};

static uint64_t align_up(uint64_t x, uint64_t a){
    return (x+a-1)&~(a-1);
}

static uint64_t align_down(uint64_t x, uint64_t a){
    return x&~(a-1);
}

static int ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end){
    return a_start<b_end && b_start<a_end;
}

uint64_t max_available_memory_address(uint64_t multiboot_data) {
    uint64_t last_addr=0;
    struct multiboot_info *info =(struct multiboot_info *)(uintptr_t)multiboot_data;
    if (info->total_size < 16) {
        return last_addr;
    }
    uint8_t *info_start = (uint8_t *)info;
    uint8_t *info_end = info_start + info->total_size;
    uint8_t *tag_ptr = info_start + 8;
    while ((uint64_t)(info_end - tag_ptr) >= 8) {
        struct multiboot_tag *tag =
            (struct multiboot_tag *)tag_ptr;
        if (tag->size < 8 ||
            (uint64_t)(info_end - tag_ptr) < tag->size) {
            break;
        }
        if (tag->type == 0)
            break;
        if (tag->type == 6 && tag->size >= 16) {
            struct multiboot_mmap_tag *mmap =(struct multiboot_mmap_tag *)tag;
            if (mmap->entry_size >= sizeof(struct multiboot_mmap_entry)) {
                uint32_t offset = 16;
                while (offset <= tag->size && mmap->entry_size <= tag->size - offset) {
                    struct multiboot_mmap_entry *entry =(struct multiboot_mmap_entry *)(tag_ptr + offset);
                    uint64_t end_addr = entry->base_addr + entry->length;
                    switch (entry->type) {
                        case 1: //available
                            if (end_addr>last_addr){
                                last_addr=end_addr;
                            }
                            break;
                        case 2: //reserved
                            break;
                        case 3: //acpi reclaimable
                            break;
                        case 4: //acpi nvs
                            break;
                        case 5: //bad ram
                            break;
                        default: //unknown
                            break;
                    }
                    offset += mmap->entry_size;
                }
            }
        }
        uint64_t aligned_size = ((uint64_t)tag->size + 7u) & ~7ull;
        if (aligned_size > (uint64_t)(info_end - tag_ptr))
            break;
        tag_ptr += aligned_size;
    }
    return last_addr;
}

struct multiboot_mmap_entry bitmap_space(uint64_t num_bytes,uint64_t multiboot_data,uintptr_t kstart,uintptr_t kend){
    struct multiboot_mmap_entry bitmap={0,0,0,0};
    num_bytes=align_up(num_bytes,FRAME_SIZE);
    struct multiboot_info *info=(struct multiboot_info *)(uintptr_t)multiboot_data;
    if (info->total_size<16)
        return bitmap;

    uint8_t *info_start=(uint8_t *)info;
    uint8_t *info_end=info_start+info->total_size;
    uint8_t *tag_ptr=info_start+8;

    uint64_t k_start=align_down((uint64_t)kstart,FRAME_SIZE);
    uint64_t k_end=align_up((uint64_t)kend,FRAME_SIZE);
    uint64_t i_start=align_down((uint64_t)(uintptr_t)info_start,FRAME_SIZE);
    uint64_t i_end=align_up((uint64_t)(uintptr_t)info_end,FRAME_SIZE);

    while ((uint64_t)(info_end-tag_ptr)>=8){
        struct multiboot_tag *tag=(struct multiboot_tag *)tag_ptr;
        if (tag->size<8 || (uint64_t)(info_end-tag_ptr)<tag->size)
            break;
        if (tag->type==0)
            break;

        if (tag->type==6 && tag->size>=16){
            struct multiboot_mmap_tag *mmap=(struct multiboot_mmap_tag *)tag;

            if (mmap->entry_size>=sizeof(struct multiboot_mmap_entry)){
                uint32_t offset=16;

                while (offset<=tag->size && mmap->entry_size<=tag->size-offset){
                    struct multiboot_mmap_entry *entry=(struct multiboot_mmap_entry *)(tag_ptr+offset);

                    if (entry->type==1){
                        uint64_t region_start=entry->base_addr;
                        uint64_t region_end=region_start+entry->length;

                        if (region_end>region_start){
                            if (region_start<LOW_MEMORY_LIMIT)
                                region_start=LOW_MEMORY_LIMIT;
                            uint64_t candidate=align_up(region_start,FRAME_SIZE);
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

                            if (candidate>=region_start && candidate+num_bytes<=region_end){
                                bitmap.base_addr=candidate;
                                bitmap.length=num_bytes;
                                bitmap.type=1;
                                bitmap.reserved=0;
                                return bitmap;
                            }
                        }
                    }

                    offset+=mmap->entry_size;
                }
            }
        }

        uint64_t aligned_size=((uint64_t)tag->size+7)&~7ull;
        if (aligned_size>(uint64_t)(info_end-tag_ptr))
            break;
        tag_ptr+=aligned_size;
    }
    return bitmap;
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

void mark_free_memory(uint64_t multiboot_data, uintptr_t kstart, uintptr_t kend, uintptr_t bitmap_start, uintptr_t bitmap_end){
    struct multiboot_info *info=(struct multiboot_info *)(uintptr_t)multiboot_data;
    if (info->total_size<16 || bitmap_end<=bitmap_start)
        return;

    uint8_t *bitmap=(uint8_t *)bitmap_start;
    uint64_t nframes=(uint64_t)(bitmap_end-bitmap_start)*8;
    uint8_t *info_start=(uint8_t *)info;
    uint8_t *info_end=info_start+info->total_size;
    uint8_t *tag_ptr=info_start+8;

    while ((uint64_t)(info_end-tag_ptr)>=8){
        struct multiboot_tag *tag=(struct multiboot_tag *)tag_ptr;
        if (tag->size<8 || (uint64_t)(info_end-tag_ptr)<tag->size)
            break;
        if (tag->type==0)
            break;

        if (tag->type==6 && tag->size>=16){
            struct multiboot_mmap_tag *mmap=(struct multiboot_mmap_tag *)tag;

            if (mmap->entry_size>=sizeof(struct multiboot_mmap_entry)){
                uint32_t offset=16;

                while (offset<=tag->size && mmap->entry_size<=tag->size-offset){
                    struct multiboot_mmap_entry *entry=(struct multiboot_mmap_entry *)(tag_ptr+offset);

                    if (entry->type==1){
                        uint64_t region_start=entry->base_addr;
                        uint64_t region_end=region_start+entry->length;

                        if (region_end>region_start){
                            if (region_start<LOW_MEMORY_LIMIT)
                                region_start=LOW_MEMORY_LIMIT;
                            uint64_t first=(region_start+FRAME_SIZE-1)/FRAME_SIZE;
                            uint64_t last=region_end/FRAME_SIZE;
                            if (first<last)
                                frames_mark_range(bitmap,nframes,first,last,0);
                        }
                    }
                    offset+=mmap->entry_size;
                }
            }
        }

        uint64_t aligned_size=((uint64_t)tag->size+7)&~7ull;
        if (aligned_size>(uint64_t)(info_end-tag_ptr))
            break;
        tag_ptr+=aligned_size;
    }

    reserve_range(bitmap,nframes,kstart,kend);
    reserve_range(bitmap,nframes,bitmap_start,bitmap_end);
    reserve_range(bitmap,nframes,(uint64_t)(uintptr_t)info_start,(uint64_t)(uintptr_t)info_end);
}

void mark_frame(uint8_t *bitmap_start,uint64_t frame,int value){
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

uint64_t alloc_frame(uint8_t *bitmap_start, uint8_t *bitmap_end){
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

uint64_t count_free_frames(uint8_t *bitmap_start,uint8_t *bitmap_end){
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
