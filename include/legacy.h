#ifndef LEGACY_H
#define LEGACY_H

#include <stdint.h>

void pit_delay_us(uint32_t usec);
void pic_disable(void);

#endif
