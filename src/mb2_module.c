#include <stdint.h>
#include <stddef.h>
#include "../include/mb2_module.h"

#define MB2_TAG_END    0u
#define MB2_TAG_MODULE 3u

struct mb2_tag_header {
    uint32_t type;
    uint32_t size;
};

struct mb2_module_tag {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
};

int mb2_find_module(const void *mbi, uint32_t max_size, uint64_t *mod_start, uint64_t *mod_end) {
    if (mbi == NULL || mod_start == NULL || mod_end == NULL || max_size < 16)
        return -1;

    const uint8_t *base = (const uint8_t *)mbi;
    uint32_t total = *(const uint32_t *)base;

    if (total < 16 || total > max_size)
        return -1;

    uint32_t off = 8;

    while (off + sizeof(struct mb2_tag_header) <= total) {
        const struct mb2_tag_header *tag = (const struct mb2_tag_header *)(base + off);

        if (tag->type == MB2_TAG_END)
            return 0;

        if (tag->size < sizeof(struct mb2_tag_header) || tag->size > total - off)
            return -1;

        if (tag->type == MB2_TAG_MODULE) {
            if (tag->size < sizeof(struct mb2_module_tag))
                return -1;

            const struct mb2_module_tag *mod = (const struct mb2_module_tag *)tag;

            if (mod->mod_end <= mod->mod_start)
                return -1;

            *mod_start = mod->mod_start;
            *mod_end = mod->mod_end;
            return 1;
        }

        uint32_t advance = (tag->size + 7u) & ~7u;

        if (advance > total - off)
            return -1;

        off += advance;
    }

    return -1;
}
