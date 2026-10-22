#include <stdint.h>
#include <stddef.h>
#include "../include/spinlock.h"
#include "../include/display.h"
#include "../include/pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu
#define PCI_MAX_BUS_DEPTH  8u

static struct pci_device devices[PCI_MAX_DEVICES];
static uint32_t device_count;
static spinlock_t pci_lock = SPINLOCK_INIT;

#ifndef PCI_HOST_TEST

static inline void port_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t port_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t hw_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1F) << 11) |
                       ((uint32_t)(func & 0x07) << 8) | (offset & 0xFCu);
    port_outl(PCI_CONFIG_ADDRESS, address);
    return port_inl(PCI_CONFIG_DATA);
}

static void hw_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1F) << 11) |
                       ((uint32_t)(func & 0x07) << 8) | (offset & 0xFCu);
    port_outl(PCI_CONFIG_ADDRESS, address);
    port_outl(PCI_CONFIG_DATA, value);
}

#else

uint32_t hw_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void hw_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

#endif

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint64_t flags = spin_lock_irqsave(&pci_lock);
    uint32_t value = hw_read32(bus, dev, func, offset);
    spin_unlock_irqrestore(&pci_lock, flags);
    return value;
}

void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint64_t flags = spin_lock_irqsave(&pci_lock);
    hw_write32(bus, dev, func, offset, value);
    spin_unlock_irqrestore(&pci_lock, flags);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (uint16_t)(pci_cfg_read32(bus, dev, func, offset) >> ((offset & 2u) * 8u));
}

uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (uint8_t)(pci_cfg_read32(bus, dev, func, offset) >> ((offset & 3u) * 8u));
}

void pci_cfg_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value) {
    uint64_t flags = spin_lock_irqsave(&pci_lock);
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t word = hw_read32(bus, dev, func, offset);
    word = (word & ~(0xFFFFu << shift)) | ((uint32_t)value << shift);
    hw_write32(bus, dev, func, offset, word);
    spin_unlock_irqrestore(&pci_lock, flags);
}

static void probe_bars(struct pci_device *d) {
    uint32_t bar_count = (d->header_type & 0x7Fu) == 0x01u ? 2u : 6u;

    if ((d->header_type & 0x7Fu) > 0x01u)
        return;

    uint16_t command = pci_cfg_read16(d->bus, d->dev, d->func, PCI_REG_COMMAND);
    pci_cfg_write16(d->bus, d->dev, d->func, PCI_REG_COMMAND, (uint16_t)(command & ~(PCI_CMD_IO | PCI_CMD_MEMORY)));

    for (uint32_t i = 0; i < bar_count; i++) {
        uint8_t off = (uint8_t)(PCI_REG_BAR0 + i * 4u);
        uint32_t orig = pci_cfg_read32(d->bus, d->dev, d->func, off);

        pci_cfg_write32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
        uint32_t mask = pci_cfg_read32(d->bus, d->dev, d->func, off);
        pci_cfg_write32(d->bus, d->dev, d->func, off, orig);

        if (mask == 0 || mask == 0xFFFFFFFFu)
            continue;

        struct pci_bar *bar = &d->bars[i];

        if (orig & 1u) {
            uint32_t m = mask & 0xFFFFFFFCu;
            if ((m & 0xFFFF0000u) == 0)
                m |= 0xFFFF0000u;
            bar->is_io = 1;
            bar->base = orig & 0xFFFFFFFCu;
            bar->size = (uint32_t)(~m + 1u);
            bar->present = 1;
            continue;
        }

        uint32_t type = (orig >> 1) & 3u;
        bar->prefetchable = (orig & 8u) ? 1 : 0;

        if (type == 2u && i + 1 < bar_count) {
            uint8_t hoff = (uint8_t)(off + 4u);
            uint32_t horig = pci_cfg_read32(d->bus, d->dev, d->func, hoff);

            pci_cfg_write32(d->bus, d->dev, d->func, hoff, 0xFFFFFFFFu);
            uint32_t hmask = pci_cfg_read32(d->bus, d->dev, d->func, hoff);
            pci_cfg_write32(d->bus, d->dev, d->func, hoff, horig);

            uint64_t full_mask = ((uint64_t)hmask << 32) | (mask & 0xFFFFFFF0u);
            bar->is_64bit = 1;
            bar->base = ((uint64_t)horig << 32) | (orig & 0xFFFFFFF0u);
            bar->size = ~full_mask + 1u;
            bar->present = 1;
            i++;
        } else {
            bar->base = orig & 0xFFFFFFF0u;
            bar->size = (uint32_t)(~(mask & 0xFFFFFFF0u) + 1u);
            bar->present = 1;
        }
    }

    pci_cfg_write16(d->bus, d->dev, d->func, PCI_REG_COMMAND, command);
}

static int record_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t id = pci_cfg_read32(bus, dev, func, PCI_REG_VENDOR);

    if ((id & 0xFFFFu) == 0xFFFFu)
        return 0;

    if (device_count >= PCI_MAX_DEVICES)
        return -1;

    struct pci_device *d = &devices[device_count++];
    uint32_t class_reg = pci_cfg_read32(bus, dev, func, PCI_REG_REVISION);

    d->bus = bus;
    d->dev = dev;
    d->func = func;
    d->vendor = (uint16_t)(id & 0xFFFFu);
    d->device = (uint16_t)(id >> 16);
    d->revision = (uint8_t)(class_reg & 0xFFu);
    d->prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
    d->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
    d->class_code = (uint8_t)(class_reg >> 24);
    d->header_type = pci_cfg_read8(bus, dev, func, PCI_REG_HEADER);
    d->irq_line = pci_cfg_read8(bus, dev, func, PCI_REG_IRQ_LINE);
    d->irq_pin = pci_cfg_read8(bus, dev, func, PCI_REG_IRQ_PIN);

    probe_bars(d);
    return 1;
}

static void scan_bus(uint8_t bus, uint32_t depth);

static void scan_function(uint8_t bus, uint8_t dev, uint8_t func, uint32_t depth) {
    if (record_function(bus, dev, func) <= 0)
        return;

    const struct pci_device *d = &devices[device_count - 1];

    if (d->class_code == 0x06 && d->subclass == 0x04 && (d->header_type & 0x7Fu) == 0x01u && depth < PCI_MAX_BUS_DEPTH) {
        uint8_t secondary = pci_cfg_read8(bus, dev, func, 0x19);
        if (secondary != 0 && secondary > bus)
            scan_bus(secondary, depth + 1);
    }
}

static void scan_bus(uint8_t bus, uint32_t depth) {
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_cfg_read16(bus, dev, 0, PCI_REG_VENDOR);

        if (vendor == 0xFFFFu)
            continue;

        uint8_t header = pci_cfg_read8(bus, dev, 0, PCI_REG_HEADER);
        uint8_t funcs = (header & 0x80u) ? 8 : 1;

        for (uint8_t func = 0; func < funcs; func++)
            scan_function(bus, dev, func, depth);
    }
}

int pci_init(void) {
    device_count = 0;
    scan_bus(0, 0);
    return (int)device_count;
}

uint32_t pci_device_count(void) {
    return device_count;
}

const struct pci_device *pci_device_get(uint32_t index) {
    return index < device_count ? &devices[index] : NULL;
}

const struct pci_device *pci_find(uint16_t vendor, uint16_t device) {
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].vendor == vendor && devices[i].device == device)
            return &devices[i];
    }

    return NULL;
}

const struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].class_code == class_code && devices[i].subclass == subclass)
            return &devices[i];
    }

    return NULL;
}

void pci_set_command(const struct pci_device *d, uint16_t bits) {
    uint16_t command = pci_cfg_read16(d->bus, d->dev, d->func, PCI_REG_COMMAND);
    pci_cfg_write16(d->bus, d->dev, d->func, PCI_REG_COMMAND, (uint16_t)(command | bits));
}

uint8_t pci_next_cap(const struct pci_device *d, uint8_t cap_id, uint8_t after_offset) {
    uint16_t status = pci_cfg_read16(d->bus, d->dev, d->func, PCI_REG_STATUS);

    if (!(status & (1u << 4)))
        return 0;

    uint8_t off;

    if (after_offset == 0)
        off = pci_cfg_read8(d->bus, d->dev, d->func, PCI_REG_CAP_PTR) & 0xFCu;
    else
        off = pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(after_offset + 1)) & 0xFCu;

    for (uint32_t guard = 0; off >= 0x40 && guard < 48; guard++) {
        if (pci_cfg_read8(d->bus, d->dev, d->func, off) == cap_id)
            return off;

        off = pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(off + 1)) & 0xFCu;
    }

    return 0;
}

void pci_dump(void) {
    for (uint32_t i = 0; i < device_count; i++) {
        const struct pci_device *d = &devices[i];

        print_hex(d->bus);
        print(":");
        print_hex(d->dev);
        print(".");
        print_uint(d->func);
        print(" ");
        print_hex(d->vendor);
        print(":");
        print_hex(d->device);
        print(" class ");
        print_hex(d->class_code);
        print(".");
        print_hex(d->subclass);
        print("\n");
    }
}
