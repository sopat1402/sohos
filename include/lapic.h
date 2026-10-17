#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

#define LAPIC_TIMER_VECTOR 0x40u
#define LAPIC_MAX_ID 256u
#define LAPIC_TICK_HZ 100u

int lapic_init(uint64_t phys);
int lapic_enable(void);
uint32_t lapic_id(void);
void lapic_eoi(void);
int lapic_send_ipi(uint32_t apic_id, uint32_t command);
int lapic_timer_calibrate(void);
uint32_t lapic_timer_ticks_per_ms(void);
int lapic_timer_install(void);
int lapic_timer_start(uint32_t hz);
void lapic_set_tick_hook(void (*hook)(void));
uint64_t lapic_timer_ticks(uint32_t apic_id);
uint64_t lapic_debug_timer_irq_count(void);

#endif
