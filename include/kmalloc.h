#ifndef KMALLOC
#define KMALLOC

#include <stddef.h>
#include <stdint.h>

int kmalloc_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
size_t ksize(void *ptr);
void kfree(void *ptr);

#endif
