#include <stdint.h>
#include <stddef.h>
#include "../include/buddy.h"
#include "../include/phys.h"
#include "../include/dma.h"

#define DMA_PAGE_SIZE 4096ULL

int dma_alloc(struct dma_buf *buf, uint64_t size) {
    if (buf == NULL || size == 0)
        return -1;

    uint64_t pages = (size + DMA_PAGE_SIZE - 1) / DMA_PAGE_SIZE;
    uint32_t order = 0;

    while ((1ULL << order) < pages)
        order++;

    uint64_t phys = buddy_alloc_order(order);

    if (phys == 0)
        return -1;

    buf->phys = phys;
    buf->virt = phys_to_virt(phys);
    buf->size = (1ULL << order) * DMA_PAGE_SIZE;
    buf->order = order;
    return 0;
}

void dma_free(struct dma_buf *buf) {
    if (buf == NULL || buf->phys == 0)
        return;

    (void)buddy_free_order(buf->phys, buf->order);
    buf->phys = 0;
    buf->virt = NULL;
    buf->size = 0;
}
