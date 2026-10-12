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
    uint8_t lapic_enabled;
    uint8_t x2apic_enabled;
    uint8_t is_bsp;
};

#define AP_BOOT_MAX_APS 32

struct ap_boot_plan {
    uint64_t trampoline_phys;
    uint64_t lapic_phys;
    uint32_t ap_count;
    uint32_t apic_ids[AP_BOOT_MAX_APS];
};

int mb2_find_acpi_rsdp(const void *mbi,
                       uint32_t mapped_bytes,
                       struct rsdp_v2 *out,
                       uint8_t *revision_out);

int cpu_probe_preapic(struct cpu_preapic_info *out);

int ap_prepare_early(const void *mbi,
                     uint64_t mbi_phys,
                     uint32_t mbi_bytes,
                     const struct rsdp_v2 *rsdp,
                     uint8_t rsdp_revision,
                     uint32_t bsp_apic_id,
                     struct ap_boot_plan *out);

int ap_start_all(const struct ap_boot_plan *plan);

#endif
