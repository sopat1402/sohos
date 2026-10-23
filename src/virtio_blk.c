#include <stdint.h>
#include <stddef.h>
#include "../include/spinlock.h"
#include "../include/display.h"
#include "../include/kmalloc.h"
#include "../include/pci.h"
#include "../include/ioremap.h"
#include "../include/dma.h"
#include "../include/virtio_blk.h"

#define VIRTIO_VENDOR 0x1AF4u
#define VIRTIO_BLK_MODERN_ID 0x1042u

#define VCAP_COMMON 1u
#define VCAP_NOTIFY 2u
#define VCAP_ISR    3u
#define VCAP_DEVICE 4u

#define VSTATUS_ACK         1u
#define VSTATUS_DRIVER      2u
#define VSTATUS_DRIVER_OK   4u
#define VSTATUS_FEATURES_OK 8u
#define VSTATUS_FAILED      128u

#define CC_DEVICE_FEATURE_SELECT 0u
#define CC_DEVICE_FEATURE        4u
#define CC_DRIVER_FEATURE_SELECT 8u
#define CC_DRIVER_FEATURE        12u
#define CC_MSIX_CONFIG           16u
#define CC_NUM_QUEUES            18u
#define CC_STATUS                20u
#define CC_QUEUE_SELECT          22u
#define CC_QUEUE_SIZE            24u
#define CC_QUEUE_MSIX_VECTOR     26u
#define CC_QUEUE_ENABLE          28u
#define CC_QUEUE_NOTIFY_OFF      30u
#define CC_QUEUE_DESC            32u
#define CC_QUEUE_DRIVER          40u
#define CC_QUEUE_DEVICE          48u

#define VIRTQ_DESC_F_NEXT  1u
#define VIRTQ_DESC_F_WRITE 2u

#define VBLK_T_IN    0u
#define VBLK_T_OUT   1u
#define VBLK_T_FLUSH 4u

#define VIRTIO_F_VERSION_1_HI 1u
#define VIRTIO_MSI_NO_VECTOR 0xFFFFu

#define BOUNCE_BYTES   65536u
#define BOUNCE_SECTORS (BOUNCE_BYTES / VIRTIO_BLK_SECTOR_SIZE)
#define POLL_LIMIT     50000000u

struct vq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vblk_req_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

static struct {
    volatile uint8_t *bar_map[6];
    uint64_t bar_size[6];
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *device_cfg;
    uint32_t notify_multiplier;
    uint16_t queue_size;
    volatile uint16_t *notify_addr;
    struct dma_buf ring;
    struct dma_buf req;
    struct dma_buf bounce;
    struct vq_desc *desc;
    volatile uint16_t *avail_flags;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_idx;
    uint16_t next_avail;
    uint16_t last_used;
    uint64_t capacity;
    int ready;
    spinlock_t lock;
} vblk = { .lock = SPINLOCK_INIT };

static volatile uint8_t *map_bar(const struct pci_device *d, uint8_t bar) {
    if (bar >= 6 || !d->bars[bar].present || d->bars[bar].is_io)
        return NULL;

    if (vblk.bar_map[bar] == NULL) {
        vblk.bar_map[bar] = ioremap(d->bars[bar].base, d->bars[bar].size);
        vblk.bar_size[bar] = d->bars[bar].size;
    }

    return vblk.bar_map[bar];
}

static int find_regions(const struct pci_device *d) {
    uint8_t cap = 0;
    int found = 0;

    while ((cap = pci_next_cap(d, 0x09, cap)) != 0) {
        uint8_t type = pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(cap + 3));
        uint8_t bar = pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(cap + 4));
        uint32_t offset = pci_cfg_read32(d->bus, d->dev, d->func, (uint8_t)(cap + 8));
        uint32_t length = pci_cfg_read32(d->bus, d->dev, d->func, (uint8_t)(cap + 12));
        volatile uint8_t *base;

        if (type != VCAP_COMMON && type != VCAP_NOTIFY && type != VCAP_DEVICE)
            continue;

        base = map_bar(d, bar);

        if (base == NULL || (uint64_t)offset + length > vblk.bar_size[bar])
            return -1;

        if (type == VCAP_COMMON) {
            vblk.common = base + offset;
            found |= 1;
        } else if (type == VCAP_NOTIFY) {
            vblk.notify = base + offset;
            vblk.notify_multiplier = pci_cfg_read32(d->bus, d->dev, d->func, (uint8_t)(cap + 16));
            found |= 2;
        } else {
            vblk.device_cfg = base + offset;
            found |= 4;
        }
    }

    return found == 7 ? 0 : -1;
}

static uint8_t cc_status(void) {
    return mmio_read8((volatile void *)(vblk.common + CC_STATUS));
}

static void cc_set_status(uint8_t value) {
    mmio_write8((volatile void *)(vblk.common + CC_STATUS), value);
}

static void write_addr64(uint32_t reg, uint64_t value) {
    mmio_write32((volatile void *)(vblk.common + reg), (uint32_t)value);
    mmio_write32((volatile void *)(vblk.common + reg + 4), (uint32_t)(value >> 32));
}

static int setup_queue(void) {
    mmio_write16((volatile void *)(vblk.common + CC_QUEUE_SELECT), 0);
    vblk.queue_size = mmio_read16((volatile void *)(vblk.common + CC_QUEUE_SIZE));

    if (vblk.queue_size == 0 || vblk.queue_size > 1024 || (vblk.queue_size & (vblk.queue_size - 1)) != 0)
        return -1;

    uint64_t qs = vblk.queue_size;
    uint64_t desc_bytes = qs * sizeof(struct vq_desc);
    uint64_t avail_off = desc_bytes;
    uint64_t avail_bytes = 6 + 2 * qs;
    uint64_t used_off = (avail_off + avail_bytes + 3u) & ~3ULL;
    uint64_t used_bytes = 6 + 8 * qs;

    if (dma_alloc(&vblk.ring, used_off + used_bytes) != 0)
        return -1;

    uint8_t *ring = (uint8_t *)vblk.ring.virt;

    vblk.desc = (struct vq_desc *)ring;
    vblk.avail_flags = (volatile uint16_t *)(ring + avail_off);
    vblk.avail_idx = (volatile uint16_t *)(ring + avail_off + 2);
    vblk.avail_ring = (volatile uint16_t *)(ring + avail_off + 4);
    vblk.used_idx = (volatile uint16_t *)(ring + used_off + 2);
    vblk.next_avail = 0;
    vblk.last_used = 0;

    write_addr64(CC_QUEUE_DESC, vblk.ring.phys);
    write_addr64(CC_QUEUE_DRIVER, vblk.ring.phys + avail_off);
    write_addr64(CC_QUEUE_DEVICE, vblk.ring.phys + used_off);
    mmio_write16((volatile void *)(vblk.common + CC_QUEUE_MSIX_VECTOR), VIRTIO_MSI_NO_VECTOR);

    uint16_t notify_off = mmio_read16((volatile void *)(vblk.common + CC_QUEUE_NOTIFY_OFF));

    vblk.notify_addr = (volatile uint16_t *)(vblk.notify + (uint64_t)notify_off * vblk.notify_multiplier);
    mmio_write16((volatile void *)(vblk.common + CC_QUEUE_ENABLE), 1);
    return 0;
}

int virtio_blk_init(void) {
    const struct pci_device *d = pci_find(VIRTIO_VENDOR, VIRTIO_BLK_MODERN_ID);

    if (d == NULL)
        return -1;

    pci_set_command(d, PCI_CMD_MEMORY | PCI_CMD_BUS_MASTER);
uint16_t cmd = pci_cfg_read16(d->bus, d->dev, d->func, PCI_REG_COMMAND);
pci_cfg_write16(d->bus, d->dev, d->func, PCI_REG_COMMAND, (uint16_t)(cmd | (1u << 10)));
    if (find_regions(d) != 0)
        return -2;

    cc_set_status(0);

    for (uint32_t i = 0; cc_status() != 0; i++) {
        if (i > POLL_LIMIT)
            return -3;

        __asm__ volatile ("pause");
    }

    cc_set_status(VSTATUS_ACK);
    cc_set_status(VSTATUS_ACK | VSTATUS_DRIVER);

    mmio_write32((volatile void *)(vblk.common + CC_DEVICE_FEATURE_SELECT), 1);

    if (!(mmio_read32((volatile void *)(vblk.common + CC_DEVICE_FEATURE)) & VIRTIO_F_VERSION_1_HI)) {
        cc_set_status(VSTATUS_FAILED);
        return -4;
    }

    mmio_write32((volatile void *)(vblk.common + CC_DRIVER_FEATURE_SELECT), 0);
    mmio_write32((volatile void *)(vblk.common + CC_DRIVER_FEATURE), 0);
    mmio_write32((volatile void *)(vblk.common + CC_DRIVER_FEATURE_SELECT), 1);
    mmio_write32((volatile void *)(vblk.common + CC_DRIVER_FEATURE), VIRTIO_F_VERSION_1_HI);

    cc_set_status(VSTATUS_ACK | VSTATUS_DRIVER | VSTATUS_FEATURES_OK);

    if (!(cc_status() & VSTATUS_FEATURES_OK)) {
        cc_set_status(VSTATUS_FAILED);
        return -5;
    }

    if (setup_queue() != 0) {
        cc_set_status(VSTATUS_FAILED);
        return -6;
    }

    if (dma_alloc(&vblk.req, 4096) != 0 || dma_alloc(&vblk.bounce, BOUNCE_BYTES) != 0) {
        cc_set_status(VSTATUS_FAILED);
        return -7;
    }

    uint32_t lo = mmio_read32((volatile void *)(vblk.device_cfg + 0));
    uint32_t hi = mmio_read32((volatile void *)(vblk.device_cfg + 4));

    vblk.capacity = ((uint64_t)hi << 32) | lo;
    cc_set_status(VSTATUS_ACK | VSTATUS_DRIVER | VSTATUS_FEATURES_OK | VSTATUS_DRIVER_OK);
    vblk.ready = 1;
    return 0;
}

uint64_t virtio_blk_sectors(void) {
    return vblk.capacity;
}

static int submit_locked(uint32_t type, uint64_t sector, uint32_t bytes) {
    struct vblk_req_header *hdr = (struct vblk_req_header *)vblk.req.virt;
    volatile uint8_t *status = (volatile uint8_t *)vblk.req.virt + 16;
    int has_data = type != VBLK_T_FLUSH;

    hdr->type = type;
    hdr->reserved = 0;
    hdr->sector = sector;
    *status = 0xFF;

    vblk.desc[0].addr = vblk.req.phys;
    vblk.desc[0].len = sizeof(*hdr);
    vblk.desc[0].flags = VIRTQ_DESC_F_NEXT;
    vblk.desc[0].next = has_data ? 1 : 2;

    if (has_data) {
        vblk.desc[1].addr = vblk.bounce.phys;
        vblk.desc[1].len = bytes;
        vblk.desc[1].flags = (uint16_t)(VIRTQ_DESC_F_NEXT | (type == VBLK_T_IN ? VIRTQ_DESC_F_WRITE : 0));
        vblk.desc[1].next = 2;
    }

    vblk.desc[2].addr = vblk.req.phys + 16;
    vblk.desc[2].len = 1;
    vblk.desc[2].flags = VIRTQ_DESC_F_WRITE;
    vblk.desc[2].next = 0;

    vblk.avail_ring[vblk.next_avail % vblk.queue_size] = 0;
    mmio_barrier();
    vblk.next_avail++;
    *vblk.avail_idx = vblk.next_avail;
    mmio_barrier();
    mmio_write16((volatile void *)vblk.notify_addr, 0);

    for (uint32_t i = 0; *vblk.used_idx == vblk.last_used; i++) {
        if (i > POLL_LIMIT)
            return -1;

        __asm__ volatile ("pause" ::: "memory");
    }

    vblk.last_used++;
    mmio_barrier();
    return *status == 0 ? 0 : -2;
}

static int transfer(uint32_t type, uint64_t sector, uint8_t *buf, uint32_t count) {
    if (!vblk.ready || buf == NULL || count == 0 || sector + count < sector || sector + count > vblk.capacity)
        return -1;

    while (count > 0) {
        uint32_t chunk = count > BOUNCE_SECTORS ? BOUNCE_SECTORS : count;
        uint32_t bytes = chunk * VIRTIO_BLK_SECTOR_SIZE;
        uint64_t flags = spin_lock_irqsave(&vblk.lock);

        if (type == VBLK_T_OUT) {
            uint8_t *dst = (uint8_t *)vblk.bounce.virt;

            for (uint32_t i = 0; i < bytes; i++)
                dst[i] = buf[i];
        }

        int rc = submit_locked(type, sector, bytes);

        if (rc == 0 && type == VBLK_T_IN) {
            const uint8_t *src = (const uint8_t *)vblk.bounce.virt;

            for (uint32_t i = 0; i < bytes; i++)
                buf[i] = src[i];
        }

        spin_unlock_irqrestore(&vblk.lock, flags);

        if (rc != 0)
            return rc;

        sector += chunk;
        buf += bytes;
        count -= chunk;
    }

    return 0;
}

int virtio_blk_read(uint64_t sector, void *buf, uint32_t count) {
    return transfer(VBLK_T_IN, sector, (uint8_t *)buf, count);
}

int virtio_blk_write(uint64_t sector, const void *buf, uint32_t count) {
    return transfer(VBLK_T_OUT, sector, (uint8_t *)(uintptr_t)buf, count);
}

int virtio_blk_flush(void) {
    if (!vblk.ready)
        return -1;

    uint64_t flags = spin_lock_irqsave(&vblk.lock);
    int rc = submit_locked(VBLK_T_FLUSH, 0, 0);

    spin_unlock_irqrestore(&vblk.lock, flags);
    return rc;
}

void virtio_blk_test(void) {
    int rc = virtio_blk_init();
    print("vblk init ");
    print_uint((uint64_t)(rc < 0 ? -rc : rc));
    print(rc < 0 ? " (err)" : " (ok)");
    print(" sectors ");
    print_uint(virtio_blk_sectors());
    print("\n");
    if (rc != 0)
        return;
    uint8_t *out = kmalloc(2 * VIRTIO_BLK_SECTOR_SIZE);
    uint8_t *in = kmalloc(2 * VIRTIO_BLK_SECTOR_SIZE);
    if (out == NULL || in == NULL) {
        print("vblk test: no memory\n");
        return;
    }
    for (uint32_t i = 0; i < 2 * VIRTIO_BLK_SECTOR_SIZE; i++) {
        out[i] = (uint8_t)(i * 7 + 3);
        in[i] = 0;
    }
    int w = virtio_blk_write(10, out, 2);
    int f = virtio_blk_flush();
    int r = virtio_blk_read(10, in, 2);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < 2 * VIRTIO_BLK_SECTOR_SIZE; i++) {
        if (in[i] != out[i])
            bad++;
    }
    int oob = virtio_blk_read(virtio_blk_sectors(), in, 1);
    print("vblk write ");
    print_uint((uint64_t)(w < 0 ? -w : w));
    print(" flush ");
    print_uint((uint64_t)(f < 0 ? -f : f));
    print(" read ");
    print_uint((uint64_t)(r < 0 ? -r : r));
    print(" mismatches ");
    print_uint(bad);
    print(" oob-rejected ");
    print_uint(oob != 0);
    print("\n");
    //kfree(out);
    //kfree(in);
}
