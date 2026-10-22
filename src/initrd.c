#include <stdint.h>
#include <stddef.h>
#include "../include/phys.h"
#include "../include/initrd.h"

#define TAR_BLOCK      512u
#define TAR_NAME_OFF   0u
#define TAR_NAME_LEN   100u
#define TAR_SIZE_OFF   124u
#define TAR_SIZE_LEN   12u
#define TAR_TYPE_OFF   156u
#define TAR_MAGIC_OFF  257u
#define TAR_PREFIX_OFF 345u
#define TAR_PREFIX_LEN 155u

static struct initrd_file files[INITRD_MAX_FILES];
static uint32_t file_count;

static uint64_t field_len(const uint8_t *p, uint64_t max) {
    uint64_t n = 0;

    while (n < max && p[n] != 0)
        n++;

    return n;
}

static int block_is_zero(const uint8_t *p) {
    for (uint32_t i = 0; i < TAR_BLOCK; i++) {
        if (p[i] != 0)
            return 0;
    }

    return 1;
}

static int parse_octal(const uint8_t *p, uint64_t len, uint64_t *out) {
    uint64_t v = 0;
    uint64_t i = 0;

    while (i < len && p[i] == ' ')
        i++;

    int digits = 0;

    while (i < len && p[i] >= '0' && p[i] <= '7') {
        v = (v << 3) | (uint64_t)(p[i] - '0');
        digits++;
        i++;
    }

    if (!digits)
        return 0;

    *out = v;
    return 1;
}

static const char *skip_prefix(const char *s) {
    for (;;) {
        if (s[0] == '/') {
            s++;
        } else if (s[0] == '.' && s[1] == '/') {
            s += 2;
        } else {
            return s;
        }
    }
}

static int name_equal(const char *a, const char *b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
}

static int build_name(const uint8_t *hdr, char *out) {
    char tmp[TAR_PREFIX_LEN + 1 + TAR_NAME_LEN + 1];
    uint64_t pos = 0;
    uint64_t prefix_len = field_len(hdr + TAR_PREFIX_OFF, TAR_PREFIX_LEN);
    uint64_t name_len = field_len(hdr + TAR_NAME_OFF, TAR_NAME_LEN);

    if (prefix_len > 0) {
        for (uint64_t i = 0; i < prefix_len; i++)
            tmp[pos++] = (char)hdr[TAR_PREFIX_OFF + i];
        tmp[pos++] = '/';
    }

    for (uint64_t i = 0; i < name_len; i++)
        tmp[pos++] = (char)hdr[TAR_NAME_OFF + i];

    tmp[pos] = 0;

    const char *start = skip_prefix(tmp);
    uint64_t n = 0;

    while (start[n] != 0) {
        if (n + 1 >= INITRD_NAME_MAX)
            return 0;
        out[n] = start[n];
        n++;
    }

    out[n] = 0;
    return n > 0;
}

int initrd_init(uint64_t mod_start, uint64_t mod_end) {
    if (mod_end <= mod_start || mod_end - mod_start < TAR_BLOCK)
        return -1;

    const uint8_t *base = (const uint8_t *)phys_to_virt(mod_start);
    uint64_t len = mod_end - mod_start;
    uint64_t off = 0;

    file_count = 0;

    while (off + TAR_BLOCK <= len) {
        const uint8_t *hdr = base + off;

        if (block_is_zero(hdr))
            break;

        if (hdr[TAR_MAGIC_OFF] != 'u' || hdr[TAR_MAGIC_OFF + 1] != 's' || hdr[TAR_MAGIC_OFF + 2] != 't' ||
            hdr[TAR_MAGIC_OFF + 3] != 'a' || hdr[TAR_MAGIC_OFF + 4] != 'r')
            return -1;

        uint64_t size = 0;

        if (!parse_octal(hdr + TAR_SIZE_OFF, TAR_SIZE_LEN, &size))
            return -1;

        if (size > len - off - TAR_BLOCK)
            return -1;

        uint8_t type = hdr[TAR_TYPE_OFF];

        if (type == '0' || type == 0) {
            if (file_count >= INITRD_MAX_FILES)
                return -1;

            struct initrd_file *f = &files[file_count];

            if (!build_name(hdr, f->name))
                return -1;

            f->data = hdr + TAR_BLOCK;
            f->size = size;
            file_count++;
        }

        uint64_t data_blocks = (size + TAR_BLOCK - 1) / TAR_BLOCK;
        off += TAR_BLOCK + data_blocks * TAR_BLOCK;
    }

    return (int)file_count;
}

uint32_t initrd_count(void) {
    return file_count;
}

const struct initrd_file *initrd_get(uint32_t index) {
    return index < file_count ? &files[index] : NULL;
}

const struct initrd_file *initrd_lookup(const char *path) {
    if (path == NULL)
        return NULL;

    const char *want = skip_prefix(path);

    for (uint32_t i = 0; i < file_count; i++) {
        if (name_equal(files[i].name, want))
            return &files[i];
    }

    return NULL;
}
