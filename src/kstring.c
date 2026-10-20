#include <stddef.h>

void *kmemcpy(void *dst, const void *src, size_t n) {
    void *ret = dst;
    __asm__ volatile ("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) :: "memory");
    return ret;
}
