#ifndef KSTRING
#define KSTRING

#include <stddef.h>

void *kmemcpy(void *dst, const void *src, size_t n);
int kstrcmp(const char *s1, const char *s2);
long strncpy_from_user(char *dst, const char *src, unsigned long count);

#endif
