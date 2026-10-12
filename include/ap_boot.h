#ifndef AP_BOOT_H
#define AP_BOOT_H

#include <stdint.h>
#include "multiboot.h"

struct cpu_preapic_info {
    char vendor[13];

    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint32_t bsp_apic_id;

    uint64_t lapic_phys_base;

    uint8_t bsp_apic_id_is_full;
    uint8_t has_msr;
    uint8_t has_apic;
    uint8_t has_x2apic;

    /* Read-only from IA32_APIC_BASE*/
    uint8_t lapic_enabled;
    uint8_t x2apic_enabled;
    uint8_t is_bsp;
};

/*
 *   1 valid RSDP found, copied to out
 *   0: no ACPI RSDP tag
 *  -1: bad Multiboot info / invalid RSDP tag
 */

int mb2_find_acpi_rsdp(const void *mbi,uint32_t mapped_bytes,struct rsdp_v2 *out,uint8_t *revision_out);

/* Reads CPUID, reads IA32_APIC_BASE. */
int cpu_probe_preapic(struct cpu_preapic_info *out);

#endif
