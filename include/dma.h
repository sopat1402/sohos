#ifndef DMA_H
#define DMA_H

#include <stdint.h>

struct dma_buf {
    void *virt;
    uint64_t phys;
    uint64_t size;
    uint32_t order;
};

int dma_alloc(struct dma_buf *buf, uint64_t size);
void dma_free(struct dma_buf *buf);

#endif
