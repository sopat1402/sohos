#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 64u

#define PCI_REG_VENDOR     0x00u
#define PCI_REG_DEVICE     0x02u
#define PCI_REG_COMMAND    0x04u
#define PCI_REG_STATUS     0x06u
#define PCI_REG_REVISION   0x08u
#define PCI_REG_PROG_IF    0x09u
#define PCI_REG_SUBCLASS   0x0Au
#define PCI_REG_CLASS      0x0Bu
#define PCI_REG_HEADER     0x0Eu
#define PCI_REG_BAR0       0x10u
#define PCI_REG_CAP_PTR    0x34u
#define PCI_REG_IRQ_LINE   0x3Cu
#define PCI_REG_IRQ_PIN    0x3Du

#define PCI_CMD_IO         (1u << 0)
#define PCI_CMD_MEMORY     (1u << 1)
#define PCI_CMD_BUS_MASTER (1u << 2)

#define PCI_CAP_VENDOR 0x09u
#define PCI_CAP_MSI    0x05u
#define PCI_CAP_MSIX   0x11u

struct pci_bar {
    uint64_t base;
    uint64_t size;
    uint8_t is_io;
    uint8_t is_64bit;
    uint8_t prefetchable;
    uint8_t present;
};

struct pci_device {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t header_type;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t irq_line;
    uint8_t irq_pin;
    struct pci_bar bars[6];
};

int pci_init(void);
uint32_t pci_device_count(void);
const struct pci_device *pci_device_get(uint32_t index);
const struct pci_device *pci_find(uint16_t vendor, uint16_t device);
const struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass);

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void pci_cfg_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);

void pci_set_command(const struct pci_device *d, uint16_t bits);
uint8_t pci_next_cap(const struct pci_device *d, uint8_t cap_id, uint8_t after_offset);
void pci_dump(void);

#endif
