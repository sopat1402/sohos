#ifndef IOREMAP_H
#define IOREMAP_H

#include <stdint.h>

void *ioremap(uint64_t phys, uint64_t size);
void iounmap(void *virt, uint64_t size);

static inline uint32_t mmio_read32(volatile void *addr) { return *(volatile uint32_t *)addr; }
static inline void mmio_write32(volatile void *addr, uint32_t value) { *(volatile uint32_t *)addr = value; }
static inline uint16_t mmio_read16(volatile void *addr) { return *(volatile uint16_t *)addr; }
static inline void mmio_write16(volatile void *addr, uint16_t value) { *(volatile uint16_t *)addr = value; }
static inline uint8_t mmio_read8(volatile void *addr) { return *(volatile uint8_t *)addr; }
static inline void mmio_write8(volatile void *addr, uint8_t value) { *(volatile uint8_t *)addr = value; }
static inline uint64_t mmio_read64(volatile void *addr) { return *(volatile uint64_t *)addr; }
static inline void mmio_write64(volatile void *addr, uint64_t value) { *(volatile uint64_t *)addr = value; }
static inline void mmio_barrier(void) { __asm__ volatile ("mfence" ::: "memory"); }

#endif
