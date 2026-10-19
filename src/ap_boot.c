#include <stdint.h>
#include "../include/ap_boot.h"
#include <stdatomic.h>
#include "../include/phys.h"
#include "../include/buddy.h"
#include "../include/idt.h"
#include "../include/gdt.h"
#include "../include/percpu.h"
#include "../include/sched.h"
#include "../include/display.h"
#include "../include/lapic.h"
#include "../include/legacy.h"
#include "../include/paging.h"

#define MB2_TAG_END       0u
#define MB2_TAG_ACPI_OLD 14u
#define MB2_TAG_ACPI_NEW 15u
#define PAGE_SIZE 4096ull
#define TWO_MIB 0x200000ull
#define CANONICAL_LIMIT (1ull << 47)
#define ADDRESS_MASK 0x000FFFFFFFFFF000ull
#define TABLE_FLAGS 0x3ull
#define RAM_FLAGS 0x3ull
#define VGA_ADDRESS 0xB8000ull
#define FLAG_PRESENT 0x1ull
#define FLAG_WRITABLE 0x2ull
#define FLAG_USER 0x4ull
#define FLAG_HUGE 0x80ull
#define FLAG_NX (1ULL << 63)
#define MMIO_FLAGS (0x1Bull | FLAG_NX)
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

#define AP_BOOTSTRAP_LIMIT 0x100000000ULL
#define AP_TRAMPOLINE_PARAMS_OFFSET 0xF00ULL
#define AP_TRAMPOLINE_PAGE_SIZE 4096ULL
#define AP_STACK_ORDER 4
#define AP_STACK_BYTES (AP_TRAMPOLINE_PAGE_SIZE << AP_STACK_ORDER)

#define MB2_TAG_MODULE 3u
#define MB2_TAG_MMAP   6u
#define MB2_MMAP_AVAILABLE 1u

struct ap_mb2_mmap_tag {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct ap_mb2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct ap_acpi_sdt_header {
    uint8_t signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oem_id[6];
    uint8_t oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct ap_trampoline_params {
    uint32_t trampoline_phys;
    uint32_t cr3_phys;
    uint64_t stack_virt_top;
    uint64_t entry_virt;
    uint32_t apic_id;
    uint32_t reserved;
};

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];

static _Atomic uint32_t ap_online_count;

static uint32_t ap_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint64_t ap_read_u64(const uint8_t *p)
{
    return (uint64_t)ap_read_u32(p)
         | ((uint64_t)ap_read_u32(p + 4) << 32);
}

static int ap_ranges_overlap(uint64_t a0, uint64_t a1,
                             uint64_t b0, uint64_t b1)
{
    return a0 < b1 && b0 < a1;
}

/* Reject a trampoline candidate if it overlaps the MBI or a GRUB module. */
static int ap_low_page_conflicts(const uint8_t *mbi,
                                 uint32_t total_size,
                                 uint64_t mbi_phys,
                                 uint64_t page_phys)
{
    uint64_t page_end = page_phys + AP_TRAMPOLINE_PAGE_SIZE;

    if (ap_ranges_overlap(page_phys, page_end,
                          mbi_phys, mbi_phys + total_size))
        return 1;

    uint32_t offset = 8;

    while (offset <= total_size &&
           total_size - offset >= sizeof(struct mb2_tag_header)) {
        const struct mb2_tag_header *tag =
            (const struct mb2_tag_header *)(mbi + offset);

        if (tag->size < sizeof(*tag) ||
            tag->size > total_size - offset)
            return 1;

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->type == MB2_TAG_MODULE) {
            if (tag->size < 16)
                return 1;

            uint64_t module_start = ap_read_u32(mbi + offset + 8);
            uint64_t module_end   = ap_read_u32(mbi + offset + 12);

            if (module_end < module_start ||
                ap_ranges_overlap(page_phys, page_end,
                                  module_start, module_end))
                return 1;
        }

        uint64_t next =
            ((uint64_t)offset + tag->size + 7ULL) & ~7ULL;

        if (next <= offset || next > total_size)
            return 1;

        offset = (uint32_t)next;
    }

    return 0;
}

/*
 * Find a page the bootloader memory map says is usable, in conventional
 * memory between 64 KiB and 512 KiB. This stays below the VGA/BIOS area
 * and below the buddy allocator's 1 MiB floor.
 */
static uint64_t ap_find_trampoline_page(const void *mbi_ptr,
                                        uint64_t mbi_phys,
                                        uint32_t mapped_bytes)
{
    if (mbi_ptr == 0 || mapped_bytes < 16)
        return 0;

    const uint8_t *mbi = (const uint8_t *)mbi_ptr;
    const struct mb2_info_header *info =
        (const struct mb2_info_header *)mbi;
    uint32_t total_size = info->total_size;

    if (total_size < 16 || total_size > mapped_bytes)
        return 0;

    uint32_t offset = 8;

    while (offset <= total_size &&
           total_size - offset >= sizeof(struct mb2_tag_header)) {
        const struct mb2_tag_header *tag =
            (const struct mb2_tag_header *)(mbi + offset);

        if (tag->size < sizeof(*tag) ||
            tag->size > total_size - offset)
            return 0;

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->type == MB2_TAG_MMAP &&
            tag->size >= sizeof(struct ap_mb2_mmap_tag)) {
            const struct ap_mb2_mmap_tag *mmap =
                (const struct ap_mb2_mmap_tag *)tag;

            if (mmap->entry_size < sizeof(struct ap_mb2_mmap_entry))
                return 0;

            uint32_t entry_offset = 16;

            while (entry_offset <= tag->size &&
                   mmap->entry_size <= tag->size - entry_offset) {
                const uint8_t *entry_bytes =
                    mbi + offset + entry_offset;
                const struct ap_mb2_mmap_entry *entry =
                    (const struct ap_mb2_mmap_entry *)entry_bytes;

                if (entry->type == MB2_MMAP_AVAILABLE &&
                    entry->length <= UINT64_MAX - entry->base) {
                    uint64_t region_start = entry->base;
                    uint64_t region_end = entry->base + entry->length;

                    if (region_start < 0x10000)
                        region_start = 0x10000;
                    if (region_end > 0x80000)
                        region_end = 0x80000;

                    uint64_t candidate =
                        (region_start + AP_TRAMPOLINE_PAGE_SIZE - 1) &
                        ~(AP_TRAMPOLINE_PAGE_SIZE - 1);

                    while (candidate <= region_end &&
                           region_end - candidate >= AP_TRAMPOLINE_PAGE_SIZE) {
                        if (!ap_low_page_conflicts(mbi, total_size,
                                                   mbi_phys, candidate))
                            return candidate;

                        candidate += AP_TRAMPOLINE_PAGE_SIZE;
                    }
                }

                entry_offset += mmap->entry_size;
            }
        }

        uint64_t next =
            ((uint64_t)offset + tag->size + 7ULL) & ~7ULL;

        if (next <= offset || next > total_size)
            return 0;

        offset = (uint32_t)next;
    }

    return 0;
}

static const struct ap_acpi_sdt_header *ap_acpi_table_at(uint64_t phys)
{
    if (phys == 0 || phys >= AP_BOOTSTRAP_LIMIT ||
        sizeof(struct ap_acpi_sdt_header) > AP_BOOTSTRAP_LIMIT - phys)
        return 0;

    const struct ap_acpi_sdt_header *h =
        (const struct ap_acpi_sdt_header *)phys_to_virt(phys);

    if (h->length < sizeof(*h) ||
        h->length > AP_BOOTSTRAP_LIMIT - phys ||
        !checksum_is_zero((const uint8_t *)h, h->length))
        return 0;

    return h;
}

static int ap_add_processor(struct ap_boot_plan *plan,
                            uint32_t apic_id,
                            uint32_t flags,
                            uint32_t bsp_apic_id)
{
    if ((flags & 1u) == 0 || apic_id == bsp_apic_id)
        return 1;

    for (uint32_t i = 0; i < plan->ap_count; i++) {
        if (plan->apic_ids[i] == apic_id)
            return 1;
    }

    if (plan->ap_count >= AP_BOOT_MAX_APS)
        return 0;

    plan->apic_ids[plan->ap_count++] = apic_id;
    return 1;
}

/* Parse RSDT/XSDT -> MADT, using the boot identity map before new_tree(). */
static int ap_parse_madt(const struct rsdp_v2 *rsdp,
                         uint8_t rsdp_revision,
                         uint32_t bsp_apic_id,
                         struct ap_boot_plan *plan)
{
    uint64_t root_phys;
    int xsdt;

    if (rsdp_revision >= 2 && rsdp->xsdt_address != 0) {
        root_phys = rsdp->xsdt_address;
        xsdt = 1;
    } else {
        root_phys = rsdp->v1.rsdt_address;
        xsdt = 0;
    }

    const struct ap_acpi_sdt_header *root =
        ap_acpi_table_at(root_phys);

    if (root == 0){
        print("[AP] Cannot read ACPI root table at physical address ");
        print_hex(root_phys);
        print("\n");
        return -1;
    }

    if ((xsdt && !bytes_equal(root->signature, "XSDT", 4)) ||
        (!xsdt && !bytes_equal(root->signature, "RSDT", 4)))
        return -1;

    uint32_t entry_bytes = xsdt ? 8 : 4;

    if (root->length < sizeof(*root) ||
        (root->length - sizeof(*root)) % entry_bytes != 0)
        return -1;

    const uint8_t *root_bytes = (const uint8_t *)root;
    uint64_t madt_phys = 0;

    for (uint32_t off = sizeof(*root);
         off < root->length;
         off += entry_bytes) {
        uint64_t child_phys = xsdt
            ? ap_read_u64(root_bytes + off)
            : ap_read_u32(root_bytes + off);

        const struct ap_acpi_sdt_header *child =
            ap_acpi_table_at(child_phys);

        if (child != 0 && bytes_equal(child->signature, "APIC", 4)) {
            madt_phys = child_phys;
            break;
        }
    }

    if (madt_phys == 0)
        return 0;

    const struct ap_acpi_sdt_header *madt =
        ap_acpi_table_at(madt_phys);
    const uint8_t *madt_bytes = (const uint8_t *)madt;

    if (madt == 0 || madt->length < 44)
        return -1;

    uint64_t lapic_phys = ap_read_u32(madt_bytes + 36);
    uint64_t lapic_override = 0;

    uint32_t off = 44;

    while (off < madt->length) {
        if (madt->length - off < 2)
            return -1;

        uint8_t type = madt_bytes[off];
        uint8_t length = madt_bytes[off + 1];

        if (length < 2 || length > madt->length - off)
            return -1;

        const uint8_t *entry = madt_bytes + off;

        if (type == 0 && length >= 8) {
            /* Processor Local APIC: APIC ID at 3, flags at 4. */
            uint32_t apic_id = entry[3];
            uint32_t flags = ap_read_u32(entry + 4);

            if (!ap_add_processor(plan, apic_id, flags, bsp_apic_id))
                return -1;
        } else if (type == 9 && length >= 16) {
            /* Processor Local x2APIC: ID at 4, flags at 8. */
            uint32_t apic_id = ap_read_u32(entry + 4);
            uint32_t flags = ap_read_u32(entry + 8);

            if (!ap_add_processor(plan, apic_id, flags, bsp_apic_id))
                return -1;
        } else if (type == 5 && length >= 12) {
            /* Local APIC Address Override. */
            lapic_override = ap_read_u64(entry + 4);
        }

        off += length;
    }

    if (lapic_override != 0)
        lapic_phys = lapic_override;

    if (lapic_phys == 0)
        return -1;

    plan->lapic_phys = lapic_phys & ~(AP_TRAMPOLINE_PAGE_SIZE - 1);
    return 1;
}

int ap_prepare_early(const void *mbi,
                     uint64_t mbi_phys,
                     uint32_t mbi_bytes,
                     const struct rsdp_v2 *rsdp,uint8_t rsdp_revision,uint32_t bsp_apic_id,struct ap_boot_plan *out){
    if (mbi == 0 || rsdp == 0 || out == 0 || hhdm_offset != 0 || mbi_phys >= AP_BOOTSTRAP_LIMIT || mbi_bytes > AP_BOOTSTRAP_LIMIT - mbi_phys)
        return -1;
    bytes_zero(out, (uint32_t)sizeof(*out));
    int madt_result = ap_parse_madt(rsdp, rsdp_revision, bsp_apic_id, out);

    if (madt_result <= 0)
        return madt_result;

    if (out->ap_count == 0)
        return 0;

    uint64_t trampoline_phys =ap_find_trampoline_page(mbi, mbi_phys, mbi_bytes);
    if (trampoline_phys == 0)
        return -1;
    uint64_t trampoline_size =(uint64_t)(uintptr_t)ap_trampoline_end -(uint64_t)(uintptr_t)ap_trampoline_start;
    if (trampoline_size == 0 ||
        trampoline_size > AP_TRAMPOLINE_PARAMS_OFFSET)
        return -1;
    /* hhdm_offset is still zero: these are identity-mapped pointers. */
    volatile uint8_t *dst =(volatile uint8_t *)(uintptr_t)trampoline_phys;
    const uint8_t *src = ap_trampoline_start;
    for (uint32_t i = 0; i < AP_TRAMPOLINE_PAGE_SIZE; i++)
        dst[i] = 0;

    for (uint64_t i = 0; i < trampoline_size; i++)
        dst[i] = src[i];

    out->trampoline_phys = trampoline_phys;
    return 1;
}

static int ap_wait_online(uint32_t previous_count) {
    for (uint32_t elapsed_ms = 0; elapsed_ms < 1000; elapsed_ms++) {
        if (atomic_load_explicit(&ap_online_count, memory_order_acquire) > previous_count)
            return 1;
        pit_delay_us(1000);
    }
    return atomic_load_explicit(&ap_online_count, memory_order_acquire) > previous_count;
}

__attribute__((noreturn))
static void ap_kernel_entry(uint32_t apic_id)
{
    (void)apic_id;
    __asm__ volatile ("cli");

    gdt_load();
    idt_load();

    if (!lapic_enable()) {
        for (;;)
            __asm__ volatile ("pause");
    }

    if (!percpu_attach(lapic_id())) {
        for (;;)
            __asm__ volatile ("pause");
    }

    if (!sched_adopt_boot_thread()) {
        for (;;)
            __asm__ volatile ("pause");
    }

    struct percpu *p = percpu_self();
    percpu_gdt_setup(p);
    setup_ist(p);
    set_tss_rsp0_currthrd();

    if (!lapic_timer_start(LAPIC_TICK_HZ)) {
        for (;;)
            __asm__ volatile ("cli\n\thlt");
    }

    atomic_fetch_add_explicit(&ap_online_count, 1, memory_order_release);
    sched_idle_loop();
}

int ap_start_all(const struct ap_boot_plan *plan) {
    if (plan == 0 || plan->ap_count == 0)
        return 1;

    if (hhdm_offset != HHDM_BASE || plan->trampoline_phys == 0 ||
        plan->lapic_phys == 0 || (plan->trampoline_phys >> 12) > 255)
        return 0;

    uint64_t cr3 = kernel_as.pml4_phys;
    if (cr3 == 0 || cr3 > 0xffffffffULL)
        return 0;

    for (uint32_t i = 0; i < plan->ap_count; i++) {
        uint32_t target = plan->apic_ids[i];
        if (target > 255)
            return 0;

        struct percpu *p = percpu_by_apic_id(target);
        if (p == 0)
            return 0;

        uint64_t stack_phys = buddy_alloc_order(AP_STACK_ORDER);
        if (stack_phys == 0)
            return 0;

        uint64_t stack_top = (uint64_t)(uintptr_t)phys_to_virt(stack_phys + AP_STACK_BYTES);

        // 1. Make trampoline Writable + Non-Executable (RW+NX) to write parameters
        if (!vm_set_flags_local(cr3, plan->trampoline_phys, FLAG_WRITABLE | FLAG_NX, 0)) {
            buddy_free_order(stack_phys, AP_STACK_ORDER);
            return 0;
        }

        volatile struct ap_trampoline_params *params =
            (volatile struct ap_trampoline_params *)(uintptr_t)(plan->trampoline_phys + AP_TRAMPOLINE_PARAMS_OFFSET);

        params->trampoline_phys = (uint32_t)plan->trampoline_phys;
        params->cr3_phys = (uint32_t)cr3;
        params->stack_virt_top = stack_top;
        params->entry_virt = (uint64_t)(uintptr_t)ap_kernel_entry;
        params->apic_id = target;
        params->reserved = 0;

        p->boot_stack_base = phys_to_virt(stack_phys);
        p->boot_stack_size = AP_STACK_BYTES;
        atomic_thread_fence(memory_order_release);

        // 2. Make trampoline Read-Only + Executable (RO+X) right before startup
        if (!vm_set_flags_local(cr3, plan->trampoline_phys, 0, FLAG_WRITABLE | FLAG_NX)) {
            buddy_free_order(stack_phys, AP_STACK_ORDER);
            return 0;
        }

        uint32_t previous = atomic_load_explicit(&ap_online_count, memory_order_acquire);

        /* INIT assert, delay, INIT deassert, then SIPI. */
        if (!lapic_send_ipi(target, 0x0000c500u))
            return 0;

        pit_delay_us(10000u);

        if (!lapic_send_ipi(target, 0x00008500u))
            return 0;

        pit_delay_us(200u);

        uint32_t sipi = 0x00004600u | (uint32_t)(plan->trampoline_phys >> 12);

        if (!lapic_send_ipi(target, sipi))
            return 0;

        pit_delay_us(200u);

        if (atomic_load_explicit(&ap_online_count, memory_order_acquire) == previous) {
            if (!lapic_send_ipi(target, sipi))
                return 0;
        }

        if (!ap_wait_online(previous))
            return 0;

        pit_delay_us(100u);
    }

    return 1;
}
