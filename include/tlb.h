#ifndef TLB_H
#define TLB_H

#include "address_space.h"

int tlb_init(void);
int tlb_cpu_online(void);
void tlb_shootdown(void);
void tlb_shootdown_as(struct address_space *as);

#endif
