#include <stddef.h>
#include "../include/usercpy.h"

void *kmemcpy(void *dst, const void *src, size_t n) {
    void *ret = dst;
    __asm__ volatile ("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) :: "memory");
    return ret;
}

int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

long strncpy_from_user(char *dst, const char *src, unsigned long count) {
    if (!access_ok(src, 1) || count == 0) {
        return -1;
    }

    unsigned long copied = 0;
    __asm__ volatile (
        "stac\n\t"
        "1:\n\t"
        "dec %3\n\t"
        "js 4f\n\t"
        "movb (%1), %%al\n\t"
        "movb %%al, (%0)\n\t"
        "testb %%al, %%al\n\t"
        "jz 3f\n\t"
        "inc %0\n\t"
        "inc %1\n\t"
        "inc %2\n\t"
        "jmp 1b\n\t"
        "3:\n\t"
        "clac\n\t"
        "jmp 5f\n\t"
        "4:\n\t"
        "clac\n\t"
        "jmp 5f\n\t"
        "2:\n\t"
        "clac\n\t"
        "movq $-1, %2\n\t"
        "5:\n\t"
        ".section __ex_table, \"a\"\n\t"
        ".quad 1b, 2b\n\t"
        ".previous\n\t"
        : "+r" (dst), "+r" (src), "+r" (copied), "+r" (count)
        :
        : "rax", "memory", "cc"
    );
    return copied;
}
