#include <stdint.h>
#include "../include/lapic.h"
#include "../include/legacy.h"
#include "../include/phys.h"
#include "../include/idt.h"
 
#define IA32_APIC_BASE_MSR 0x1bu
#define APIC_BASE_ADDRESS_MASK 0x0000000ffffff000ULL
#define APIC_BASE_X2APIC (1ULL << 10)
#define APIC_BASE_ENABLE (1ULL << 11)
 
#define LAPIC_REG_ID 0x020u
#define LAPIC_REG_TPR 0x080u
#define LAPIC_REG_EOI 0x0B0u
#define LAPIC_REG_SVR 0x0F0u
#define LAPIC_REG_ICR_LOW 0x300u
#define LAPIC_REG_ICR_HIGH 0x310u
#define LAPIC_REG_LVT_TIMER 0x320u
#define LAPIC_REG_TIMER_INIT 0x380u
#define LAPIC_REG_TIMER_CUR 0x390u
#define LAPIC_REG_TIMER_DIV 0x3E0u
 
#define LAPIC_SVR_ENABLE 0x100u
#define LAPIC_ICR_PENDING (1u << 12)
#define LAPIC_LVT_MASKED (1u << 16)
#define LAPIC_LVT_PERIODIC (1u << 17)
#define LAPIC_TIMER_DIV_16 0x3u
#define LAPIC_CALIBRATE_US 20000u
#define LAPIC_IPI_TIMEOUT 1000000u
#define LAPIC_MAX_HZ 10000u
 
static volatile uint32_t *lapic_regs;
static uint64_t lapic_phys_base;
static uint32_t timer_ticks_per_ms;
static int timer_installed;
static uint64_t tick_counts[LAPIC_MAX_ID];
 
static uint64_t lapic_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
    return ((uint64_t)high << 32) | low;
}
 
static void lapic_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}
 
static volatile uint32_t *regs(void) {
    return (volatile uint32_t *)__atomic_load_n(&lapic_regs, __ATOMIC_ACQUIRE);
}
 
static uint32_t read_reg(uint32_t offset) {
    return regs()[offset / 4];
}
 
static void write_reg(uint32_t offset, uint32_t value) {
    regs()[offset / 4] = value;
}
 
int lapic_init(uint64_t phys) {
    if (phys == 0 || (phys & 0xfffULL) != 0)
        return 0;
    lapic_phys_base = phys;
    __atomic_store_n(&lapic_regs, (volatile uint32_t *)phys_to_virt(phys), __ATOMIC_RELEASE);
    return 1;
}
 
int lapic_enable(void) {
    if (regs() == 0)
        return 0;
    uint64_t base = lapic_rdmsr(IA32_APIC_BASE_MSR);
    if ((base & APIC_BASE_X2APIC) != 0 || (base & APIC_BASE_ADDRESS_MASK) != lapic_phys_base)
        return 0;
    if ((base & APIC_BASE_ENABLE) == 0)
        lapic_wrmsr(IA32_APIC_BASE_MSR, base | APIC_BASE_ENABLE);
    write_reg(LAPIC_REG_TPR, 0);
    write_reg(LAPIC_REG_SVR, LAPIC_SVR_ENABLE | IRQ_VECTOR_SPURIOUS);
    return 1;
}
 
uint32_t lapic_id(void) {
    return read_reg(LAPIC_REG_ID) >> 24;
}
 
void lapic_eoi(void) {
    write_reg(LAPIC_REG_EOI, 0);
}
 
static int wait_icr_idle(void) {
    uint32_t timeout = LAPIC_IPI_TIMEOUT;
    while ((read_reg(LAPIC_REG_ICR_LOW) & LAPIC_ICR_PENDING) != 0) {
        if (--timeout == 0)
            return 0;
        __asm__ volatile ("pause");
    }
    return 1;
}
 
int lapic_send_ipi(uint32_t apic_id, uint32_t command) {
    if (apic_id > 255 || regs() == 0)
        return 0;
    if (!wait_icr_idle())
        return 0;
    write_reg(LAPIC_REG_ICR_HIGH, apic_id << 24);
    write_reg(LAPIC_REG_ICR_LOW, command);
    return wait_icr_idle();
}
 
int lapic_timer_calibrate(void) {
    if (regs() == 0)
        return 0;
    write_reg(LAPIC_REG_TIMER_DIV, LAPIC_TIMER_DIV_16);
    write_reg(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED | LAPIC_TIMER_VECTOR);
    write_reg(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);
    pit_delay_us(LAPIC_CALIBRATE_US);
    uint32_t remaining = read_reg(LAPIC_REG_TIMER_CUR);
    write_reg(LAPIC_REG_TIMER_INIT, 0);
    uint32_t elapsed = 0xFFFFFFFFu - remaining;
    uint32_t per_ms = elapsed / (LAPIC_CALIBRATE_US / 1000u);
    if (per_ms == 0)
        return 0;
    __atomic_store_n(&timer_ticks_per_ms, per_ms, __ATOMIC_RELEASE);
    return 1;
}
 
uint32_t lapic_timer_ticks_per_ms(void) {
    return __atomic_load_n(&timer_ticks_per_ms, __ATOMIC_ACQUIRE);
}
 
static void lapic_timer_handler(struct interrupt_frame *frame) {
    (void)frame;
    uint32_t id = lapic_id();
    if (id < LAPIC_MAX_ID)
        __atomic_fetch_add(&tick_counts[id], 1, __ATOMIC_RELAXED);
    lapic_eoi();
}
 
int lapic_timer_install(void) {
    if (timer_installed)
        return 1;
    if (!irq_register(LAPIC_TIMER_VECTOR, lapic_timer_handler))
        return 0;
    timer_installed = 1;
    return 1;
}
 
int lapic_timer_start(uint32_t hz) {
    uint32_t per_ms = lapic_timer_ticks_per_ms();
    if (regs() == 0 || per_ms == 0 || hz == 0 || hz > LAPIC_MAX_HZ)
        return 0;
    uint64_t count = (uint64_t)per_ms * 1000u / hz;
    if (count == 0 || count > 0xFFFFFFFFull)
        return 0;
    write_reg(LAPIC_REG_TIMER_DIV, LAPIC_TIMER_DIV_16);
    write_reg(LAPIC_REG_LVT_TIMER, LAPIC_LVT_PERIODIC | LAPIC_TIMER_VECTOR);
    write_reg(LAPIC_REG_TIMER_INIT, (uint32_t)count);
    return 1;
}
 
uint64_t lapic_timer_ticks(uint32_t apic_id) {
    if (apic_id >= LAPIC_MAX_ID)
        return 0;
    return __atomic_load_n(&tick_counts[apic_id], __ATOMIC_RELAXED);
}
