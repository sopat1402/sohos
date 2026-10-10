#include <stdint.h>
#include "../include/phys.h"

uint64_t hhdm_offset = 0;

void phys_set_hhdm_offset(uint64_t offset){
    hhdm_offset = offset;
}
