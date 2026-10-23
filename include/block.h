#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

#define BLOCK_MAX_DEVICES 8u
#define BLOCK_NAME_MAX 16u

struct block_device;

struct block_ops {
    int (*read)(struct block_device *dev, uint64_t lba, void *buf, uint32_t count);
    int (*write)(struct block_device *dev, uint64_t lba, const void *buf, uint32_t count);
    int (*flush)(struct block_device *dev);
};

struct block_device {
    char name[BLOCK_NAME_MAX];
    uint32_t sector_size;
    uint64_t sector_count;
    const struct block_ops *ops;
    void *priv;
};

int block_register(struct block_device *dev);
struct block_device *block_find(const char *name);
uint32_t block_count(void);
struct block_device *block_get(uint32_t index);

int block_read(struct block_device *dev, uint64_t lba, void *buf, uint32_t count);
int block_write(struct block_device *dev, uint64_t lba, const void *buf, uint32_t count);
int block_flush(struct block_device *dev);

int block_register_virtio(void);
void block_test(void);

#endif
