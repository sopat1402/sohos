#include <stdint.h>
#include <stddef.h>
#include "../include/spinlock.h"
#include "../include/display.h"
#include "../include/kmalloc.h"
#include "../include/virtio_blk.h"
#include "../include/block.h"
#include "../include/mutex.h"

static struct block_device *registry[BLOCK_MAX_DEVICES];
static uint32_t registry_count;
static spinlock_t registry_lock = SPINLOCK_INIT;
static struct block_device virtio_dev;

static int names_equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
}

int block_register(struct block_device *dev) {
    if (dev == NULL || dev->ops == NULL || dev->ops->read == NULL || dev->ops->write == NULL || dev->sector_size == 0 || dev->sector_count == 0 || dev->name[0] == '\0')
        return -1;

    uint64_t flags = spin_lock_irqsave(&registry_lock);
    int rc = 0;

    if (registry_count >= BLOCK_MAX_DEVICES) {
        rc = -1;
    } else {
        for (uint32_t i = 0; i < registry_count; i++) {
            if (names_equal(registry[i]->name, dev->name)) {
                rc = -1;
                break;
            }
        }

        if (rc == 0)
            registry[registry_count++] = dev;
    }

    spin_unlock_irqrestore(&registry_lock, flags);
    return rc;
}

struct block_device *block_find(const char *name) {
    struct block_device *found = NULL;

    if (name == NULL)
        return NULL;

    uint64_t flags = spin_lock_irqsave(&registry_lock);

    for (uint32_t i = 0; i < registry_count; i++) {
        if (names_equal(registry[i]->name, name)) {
            found = registry[i];
            break;
        }
    }

    spin_unlock_irqrestore(&registry_lock, flags);
    return found;
}

uint32_t block_count(void) {
    return registry_count;
}

struct block_device *block_get(uint32_t index) {
    return index < registry_count ? registry[index] : NULL;
}

static int range_ok(const struct block_device *dev, uint64_t lba, uint32_t count) {
    return dev != NULL && count != 0 && lba + count >= lba && lba + count <= dev->sector_count;
}

int block_read(struct block_device *dev, uint64_t lba, void *buf, uint32_t count) {
    if (buf == NULL || !range_ok(dev, lba, count))
        return -1;

    return dev->ops->read(dev, lba, buf, count);
}

int block_write(struct block_device *dev, uint64_t lba, const void *buf, uint32_t count) {
    if (buf == NULL || !range_ok(dev, lba, count))
        return -1;

    return dev->ops->write(dev, lba, buf, count);
}

int block_flush(struct block_device *dev) {
    if (dev == NULL)
        return -1;

    return dev->ops->flush != NULL ? dev->ops->flush(dev) : 0;
}

static int virtio_read(struct block_device *dev, uint64_t lba, void *buf, uint32_t count) {
    (void)dev;
    return virtio_blk_read(lba, buf, count);
}

static int virtio_write(struct block_device *dev, uint64_t lba, const void *buf, uint32_t count) {
    (void)dev;
    return virtio_blk_write(lba, buf, count);
}

static int virtio_flush(struct block_device *dev) {
    (void)dev;
    return virtio_blk_flush();
}

static const struct block_ops virtio_ops = {
    .read = virtio_read,
    .write = virtio_write,
    .flush = virtio_flush,
};

int block_register_virtio(void) {
    uint64_t sectors = virtio_blk_sectors();

    if (sectors == 0)
        return -1;

    virtio_dev.name[0] = 'v';
    virtio_dev.name[1] = 'd';
    virtio_dev.name[2] = 'a';
    virtio_dev.name[3] = '\0';
    virtio_dev.sector_size = VIRTIO_BLK_SECTOR_SIZE;
    virtio_dev.sector_count = sectors;
    virtio_dev.ops = &virtio_ops;
    virtio_dev.priv = NULL;
    return block_register(&virtio_dev);
}

void block_test(void) {
    struct block_device *dev = block_find("vda");

    if (dev == NULL) {
        print("block: vda not found\n");
        return;
    }

    uint32_t sectors = 8;
    uint32_t bytes = sectors * dev->sector_size;
    uint8_t *out = kmalloc(bytes);
    uint8_t *in = kmalloc(bytes);

    if (out == NULL || in == NULL) {
        print("block: no memory\n");
        return;
    }

    for (uint32_t i = 0; i < bytes; i++) {
        out[i] = (uint8_t)((i * 13u) ^ 0x5Au);
        in[i] = 0;
    }

    int w = block_write(dev, 64, out, sectors);
    int f = block_flush(dev);
    int r = block_read(dev, 64, in, sectors);
    uint32_t bad = 0;

    for (uint32_t i = 0; i < bytes; i++) {
        if (in[i] != out[i])
            bad++;
    }

    int oob_read = block_read(dev, dev->sector_count - 1, in, 2);
    int oob_write = block_write(dev, dev->sector_count, out, 1);
    int zero = block_read(dev, 0, in, 0);

    print("block ");
    print(dev->name);
    print(" sectors ");
    print_uint(dev->sector_count);
    print(" w ");
    print_uint((uint64_t)(w < 0 ? -w : w));
    print(" f ");
    print_uint((uint64_t)(f < 0 ? -f : f));
    print(" r ");
    print_uint((uint64_t)(r < 0 ? -r : r));
    print(" bad ");
    print_uint(bad);
    print(" rejected ");
    print_uint((oob_read != 0) + (oob_write != 0) + (zero != 0));
    print("/3\n");

    kfree(out);
    kfree(in);
}

#define FS_BLOCK_SECTORS 8u

static struct block_device *fs_dev;
static mutex_t fs_mutex;

int fs_glue_init(void) {
    mutex_init(&fs_mutex);
    fs_dev = block_find("vda");
    return fs_dev == NULL ? -1 : 0;
}

void kernel_fs_lock(void) {
    mutex_lock(&fs_mutex);
}

void kernel_fs_unlock(void) {
    mutex_unlock(&fs_mutex);
}

static int args_ok(uint64_t block, uint32_t count) {
    return fs_dev != NULL && count != 0 && count <= UINT32_MAX / FS_BLOCK_SECTORS && block <= UINT64_MAX / FS_BLOCK_SECTORS;
}

int kernel_block_read(uint64_t block, void *buf, uint32_t count) {
    if (!args_ok(block, count))
        return -1;

    return block_read(fs_dev, block * FS_BLOCK_SECTORS, buf, count * FS_BLOCK_SECTORS);
}

int kernel_block_write(uint64_t block, const void *buf, uint32_t count) {
    if (!args_ok(block, count))
        return -1;

    return block_write(fs_dev, block * FS_BLOCK_SECTORS, buf, count * FS_BLOCK_SECTORS);
}

int kernel_block_flush(void) {
    if (fs_dev == NULL)
        return -1;

    return block_flush(fs_dev);
}
