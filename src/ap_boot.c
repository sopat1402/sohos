#include <stdint.h>
#include "../include/ap_boot.h"

#define MB2_TAG_END       0u
#define MB2_TAG_ACPI_OLD 14u
#define MB2_TAG_ACPI_NEW 15u

#define IA32_APIC_BASE_MSR 0x1bu
#define APIC_BASE_ADDRESS_MASK 0x0000000ffffff000ULL

struct mb2_info_header {
    uint32_t total_size;
    uint32_t reserved;
};

struct mb2_tag_header {
    uint32_t type;
    uint32_t size;
};

struct cpuid_regs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static void bytes_zero(void *dst, uint32_t count)
{
    uint8_t *d = (uint8_t *)dst;

    for (uint32_t i = 0; i < count; i++)
        d[i] = 0;
}

static void bytes_copy(void *dst, const void *src, uint32_t count)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    for (uint32_t i = 0; i < count; i++)
        d[i] = s[i];
}

static int bytes_equal(const uint8_t *a, const char *b, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (a[i] != (uint8_t)b[i])
            return 0;
    }

    return 1;
}

static int checksum_is_zero(const uint8_t *bytes, uint32_t count){
    uint8_t sum = 0;
    for (uint32_t i = 0; i < count; i++)
        sum = (uint8_t)(sum + bytes[i]);
    return sum == 0;
}

int mb2_find_acpi_rsdp(const void *mbi,uint32_t mapped_bytes,struct rsdp_v2 *out,uint8_t *revision_out){
    if (mbi == 0 || out == 0 || revision_out == 0 ||
        mapped_bytes < 16) {
        return -1;
    }
    const uint8_t *base = (const uint8_t *)mbi;
    const struct mb2_info_header *info =(const struct mb2_info_header *)base;
    const uint32_t total_size = info->total_size;
    if (total_size < 16 || total_size > mapped_bytes)
        return -1;
    struct rsdp_v2 best;
    bytes_zero(&best, (uint32_t)sizeof(best));
    uint8_t best_revision = 0;
    int best_priority = 0;
    int saw_bad_rsdp = 0;
    int saw_end_tag = 0;

    uint32_t offset = 8; // mb2 header

    while (offset <= total_size &&total_size - offset >= sizeof(struct mb2_tag_header)) {
        const struct mb2_tag_header *tag =(const struct mb2_tag_header *)(base + offset);
        if (tag->size < sizeof(struct mb2_tag_header) ||tag->size > total_size - offset) {
            return -1;
        }
        if (tag->type == MB2_TAG_END) {
            if (tag->size != sizeof(struct mb2_tag_header))
                return -1;
            saw_end_tag = 1;
            break;
        }
        const uint8_t *payload =base + offset + sizeof(struct mb2_tag_header);
        const uint32_t payload_size =tag->size - (uint32_t)sizeof(struct mb2_tag_header);
        if (tag->type == MB2_TAG_ACPI_OLD) {
            if (payload_size < sizeof(struct rsdp_v1)) {
                saw_bad_rsdp = 1;
            } else if (
                bytes_equal(payload, "RSD PTR ", 8) &&
                checksum_is_zero(payload, sizeof(struct rsdp_v1))) {
                // if v2, save it else take v1
                if (best_priority < 1) {
                    bytes_zero(&best, (uint32_t)sizeof(best));
                    bytes_copy(&best, payload, sizeof(struct rsdp_v1));
                    best_revision = best.v1.revision;
                    best_priority = 1;
                }
            } else {
                saw_bad_rsdp = 1;
            }
        } else if (tag->type == MB2_TAG_ACPI_NEW) {
            if (payload_size < sizeof(struct rsdp_v2)) {
                saw_bad_rsdp = 1;
            } else {
                const struct rsdp_v2 *candidate =(const struct rsdp_v2 *)payload;
                const uint32_t rsdp_length = candidate->length;
                if (candidate->v1.revision < 2 ||
                    rsdp_length < sizeof(struct rsdp_v2) ||
                    rsdp_length > payload_size ||
                    !bytes_equal(payload, "RSD PTR ", 8) ||
                    !checksum_is_zero(payload, sizeof(struct rsdp_v1)) ||
                    !checksum_is_zero(payload, rsdp_length)) {
                    saw_bad_rsdp = 1;
                } else if (best_priority < 2) {
                    bytes_copy(&best, payload, sizeof(best));
                    best_revision = candidate->v1.revision;
                    best_priority = 2;
                }
            }
        }

        /* mb2 tag from 8 byte limit */
        uint64_t next =((uint64_t)offset + tag->size + 7ULL) & ~7ULL;
        if (next <= offset || next > total_size)
            return -1;
        offset = (uint32_t)next;
    }
    if (!saw_end_tag)
        return -1;
    if (best_priority != 0) {
        bytes_copy(out, &best, sizeof(best));
        *revision_out = best_revision;
        return 1;
    }
    return saw_bad_rsdp ? -1 : 0;
}

static void cpuid_count(uint32_t leaf,uint32_t subleaf,struct cpuid_regs *out){
    __asm__ volatile ("cpuid": "=a"(out->eax), "=b"(out->ebx),"=c"(out->ecx), "=d"(out->edx): "a"(leaf), "c"(subleaf));
}

static void store_u32_as_le_bytes(char *dst, uint32_t value){
    dst[0] = (char)(value & 0xffu);
    dst[1] = (char)((value >> 8) & 0xffu);
    dst[2] = (char)((value >> 16) & 0xffu);
    dst[3] = (char)((value >> 24) & 0xffu);
}

static int get_topology_apic_id(uint32_t leaf, uint32_t *apic_id){
    struct cpuid_regs r;
    cpuid_count(leaf, 0, &r);
    uint32_t logical_count = r.ebx & 0xffffu;
    uint32_t level_type = (r.ecx >> 8) & 0xffu;
    if (logical_count == 0 || level_type == 0)
        return 0;
    *apic_id = r.edx;
    return 1;
}

static uint64_t read_msr(uint32_t msr){
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr": "=a"(low), "=d"(high): "c"(msr): "memory");
    return ((uint64_t)high << 32) | low;
}

int cpu_probe_preapic(struct cpu_preapic_info *out){
    if (out == 0)
        return 0;
    bytes_zero(out, (uint32_t)sizeof(*out));
    struct cpuid_regs r0;
    cpuid_count(0, 0, &r0);
    out->max_basic_leaf = r0.eax;
    // cpuid vendor order ebc, edx, ecx
    store_u32_as_le_bytes(&out->vendor[0], r0.ebx);
    store_u32_as_le_bytes(&out->vendor[4], r0.edx);
    store_u32_as_le_bytes(&out->vendor[8], r0.ecx);
    out->vendor[12] = '\0';
    struct cpuid_regs rext;
    cpuid_count(0x80000000u, 0, &rext);
    if (rext.eax >= 0x80000000u)
        out->max_extended_leaf = rext.eax;
    if (out->max_basic_leaf < 1)
        return 1;
    struct cpuid_regs r1;
    cpuid_count(1, 0, &r1);
    // CPUID.1 edx[5] = MSR, EDX[9] = APIC.
    out->has_msr = (uint8_t)((r1.edx >> 5) & 1u);
    out->has_apic = (uint8_t)((r1.edx >> 9) & 1u);

    // CPUID.1 ecv[21] = x2APIC support.
    out->has_x2apic = (uint8_t)((r1.ecx >> 21) & 1u);

    /* Legacy 8-bit APIC ID fallback from CPUID.1:EBX[31:24]. */
    out->bsp_apic_id = (r1.ebx >> 24) & 0xffu;
    out->bsp_apic_id_is_full = 0;

    // Want the full x2apic id
    if (out->max_basic_leaf >= 0x1fu &&
        get_topology_apic_id(0x1fu, &out->bsp_apic_id)) {
        out->bsp_apic_id_is_full = 1;
    } else if (out->max_basic_leaf >= 0x0bu &&
               get_topology_apic_id(0x0bu, &out->bsp_apic_id)) {
        out->bsp_apic_id_is_full = 1;
    }
    //read msr and ia32 apic base
    if (out->has_msr && out->has_apic) {
        uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);

        out->lapic_phys_base =
            apic_base & APIC_BASE_ADDRESS_MASK;
        out->is_bsp = (uint8_t)((apic_base >> 8) & 1u);
        out->x2apic_enabled = (uint8_t)((apic_base >> 10) & 1u);
        out->lapic_enabled = (uint8_t)((apic_base >> 11) & 1u);
    }

    return 1;
}
