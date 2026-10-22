#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

#define INITRD_MAX_FILES 128u
#define INITRD_NAME_MAX  128u

struct initrd_file {
    char name[INITRD_NAME_MAX];
    const uint8_t *data;
    uint64_t size;
};

int initrd_init(uint64_t mod_start, uint64_t mod_end);
uint32_t initrd_count(void);
const struct initrd_file *initrd_get(uint32_t index);
const struct initrd_file *initrd_lookup(const char *path);

#endif
