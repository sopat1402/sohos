#include <stdint.h>
#include "../include/multiboot.h"
#include "../include/memory_regions.h"
#include "../include/phys.h"

#define REGION_FRAME_SIZE 4096ull
#define REGION_FLOOR 0x100000ull

static struct mem_region regions[MAX_MEM_REGIONS];
static uint32_t region_count=0;

static int add_region(uint64_t base, uint64_t end){
    if (region_count>=MAX_MEM_REGIONS)
        return 0;
    uint32_t i=region_count;
    while (i>0 && regions[i-1].base>base){
        regions[i]=regions[i-1];
        i--;
    }
    regions[i].base=base;
    regions[i].end=end;
    region_count++;
    return 1;
}

static void merge_regions(void){
    uint32_t out=0;
    for (uint32_t i=0;i<region_count;i++){
        if (out>0 && regions[i].base<=regions[out-1].end){
            if (regions[i].end>regions[out-1].end)
                regions[out-1].end=regions[i].end;
        }else{
            regions[out++]=regions[i];
        }
    }
    region_count=out;
}

int memory_regions_init(uint64_t multiboot_data){
    region_count=0;
    if (multiboot_data==0)
        return 0;

    struct multiboot_info *info=(struct multiboot_info *)phys_to_virt(multiboot_data);
    if (info->total_size<16)
        return 0;

    uint8_t *info_start=(uint8_t *)info;
    uint8_t *info_end=info_start+info->total_size;
    uint8_t *tag_ptr=info_start+8;

    while ((uint64_t)(info_end-tag_ptr)>=8){
        struct multiboot_tag *tag=(struct multiboot_tag *)tag_ptr;
        if (tag->size<8 || (uint64_t)(info_end-tag_ptr)<tag->size)
            break;
        if (tag->type==MULTIBOOT_TAG_END)
            break;

        if (tag->type==MULTIBOOT_TAG_MMAP && tag->size>=16){
            struct multiboot_mmap_tag *mmap=(struct multiboot_mmap_tag *)tag;

            if (mmap->entry_size>=sizeof(struct multiboot_mmap_entry)){
                uint32_t offset=16;

                while (offset<=tag->size && mmap->entry_size<=tag->size-offset){
                    struct multiboot_mmap_entry *entry=(struct multiboot_mmap_entry *)(tag_ptr+offset);

                    if (entry->type==MULTIBOOT_MMAP_AVAILABLE && entry->length<=~0ull-entry->base_addr){
                        uint64_t base=entry->base_addr;
                        uint64_t end=(base+entry->length)&~(REGION_FRAME_SIZE-1);
                        if (base<REGION_FLOOR)
                            base=REGION_FLOOR;
                        if (base<end){
                            base=(base+REGION_FRAME_SIZE-1)&~(REGION_FRAME_SIZE-1);
                            if (base<end && !add_region(base,end))
                                return 0;
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

    merge_regions();
    return region_count>0;
}

uint32_t memory_region_count(void){
    return region_count;
}

const struct mem_region *memory_region_get(uint32_t index){
    if (index>=region_count)
        return 0;
    return &regions[index];
}

uint64_t memory_regions_total_bytes(void){
    uint64_t total=0;
    for (uint32_t i=0;i<region_count;i++)
        total+=regions[i].end-regions[i].base;
    return total;
}

uint64_t memory_regions_highest_end(void){
    return region_count>0 ? regions[region_count-1].end : 0;
}
