#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include <stdint.h>

#define VIRTIO_BLK_SECTOR_SIZE 512u

int virtio_blk_init(void);
uint64_t virtio_blk_sectors(void);
int virtio_blk_read(uint64_t sector, void *buf, uint32_t count);
int virtio_blk_write(uint64_t sector, const void *buf, uint32_t count);
int virtio_blk_flush(void);
void virtio_blk_test(void);

#endif
