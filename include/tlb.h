#ifndef TLB_H
#define TLB_H

int tlb_init(void);
int tlb_cpu_online(void);
void tlb_shootdown(void);

#endif
