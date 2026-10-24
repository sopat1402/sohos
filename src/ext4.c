#include <stdint.h>
#include <stddef.h>
#ifdef EXT4_HOST
#include <stdlib.h>
#include "../include/ext4.h"
#define ext4_alloc(n) malloc(n)
#define ext4_free(p) free(p)
#else
#include "../include/kmalloc.h"
#include "../include/ext4.h"
#define ext4_alloc(n) kmalloc(n)
#define ext4_free(p) kfree(p)
#endif

#define SB_OFFSET_SECTOR 2u
#define EXT4_MAGIC 0xEF53u
#define EXTENT_MAGIC 0xF30Au
#define EXT4_EXTENTS_FL 0x80000u
#define INCOMPAT_FILETYPE   0x0002u
#define INCOMPAT_EXTENTS    0x0040u
#define INCOMPAT_64BIT      0x0080u
#define INCOMPAT_FLEX_BG    0x0200u
#define INCOMPAT_CSUM_SEED  0x2000u
#define INCOMPAT_SUPPORTED  (INCOMPAT_FILETYPE | INCOMPAT_EXTENTS | INCOMPAT_64BIT | INCOMPAT_FLEX_BG | INCOMPAT_CSUM_SEED)
#define MAX_EXTENT_DEPTH 6u
#define MAX_PATH_DEPTH 64u
#define NAME_MAX_LEN 255u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void copy_bytes(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    for (uint64_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void zero_bytes(void *dst, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;

    for (uint64_t i = 0; i < n; i++)
        d[i] = 0;
}

static int read_block(struct ext4_fs *fs, uint64_t block, void *buf) {
    uint32_t sectors = fs->block_size / 512u;

    if (block >= fs->blocks_count)
        return EXT4_E_FORMAT;

    return fs->read(fs->ctx, block * sectors, buf, sectors) == 0 ? 0 : EXT4_E_IO;
}

int ext4_mount(struct ext4_fs *fs, ext4_read_fn read, void *ctx) {
    if (fs == NULL || read == NULL)
        return EXT4_E_FORMAT;

    uint8_t *sb = (uint8_t *)ext4_alloc(1024);

    if (sb == NULL)
        return EXT4_E_NOMEM;

    int rc = 0;

    if (read(ctx, SB_OFFSET_SECTOR, sb, 2) != 0) {
        rc = EXT4_E_IO;
        goto out;
    }

    if (rd16(sb + 56) != EXT4_MAGIC) {
        rc = EXT4_E_FORMAT;
        goto out;
    }

    uint32_t log_bs = rd32(sb + 24);
    uint32_t incompat = rd32(sb + 96);

    if (log_bs > 6u || (incompat & ~INCOMPAT_SUPPORTED) != 0) {
        rc = (incompat & ~INCOMPAT_SUPPORTED) != 0 ? EXT4_E_UNSUPPORTED : EXT4_E_FORMAT;
        goto out;
    }

    fs->read = read;
    fs->ctx = ctx;
    fs->block_size = 1024u << log_bs;
    fs->first_data_block = rd32(sb + 20);
    fs->inodes_per_group = rd32(sb + 40);
    fs->is64 = (incompat & INCOMPAT_64BIT) != 0;
    fs->inode_size = rd32(sb + 76) == 0 ? 128u : rd16(sb + 88);
    fs->desc_size = fs->is64 ? rd16(sb + 254) : 32u;
    fs->blocks_count = rd32(sb + 4);

    if (fs->is64)
        fs->blocks_count |= (uint64_t)rd32(sb + 0x150) << 32;

    uint32_t blocks_per_group = rd32(sb + 32);

    if (blocks_per_group == 0 || fs->inodes_per_group == 0 || fs->inode_size < 128u || fs->inode_size > fs->block_size ||
        (fs->inode_size & (fs->inode_size - 1)) != 0 || fs->desc_size < 32u || fs->desc_size > fs->block_size) {
        rc = EXT4_E_FORMAT;
        goto out;
    }

    fs->groups = (uint32_t)((fs->blocks_count + blocks_per_group - 1) / blocks_per_group);

out:
    ext4_free(sb);
    return rc;
}

int ext4_read_inode(struct ext4_fs *fs, uint32_t ino, struct ext4_inode *out) {
    if (ino == 0 || out == NULL)
        return EXT4_E_FORMAT;

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;

    if (group >= fs->groups)
        return EXT4_E_FORMAT;

    uint8_t *buf = (uint8_t *)ext4_alloc(fs->block_size);

    if (buf == NULL)
        return EXT4_E_NOMEM;

    uint64_t gd_off = (uint64_t)group * fs->desc_size;
    int rc = read_block(fs, (uint64_t)fs->first_data_block + 1 + gd_off / fs->block_size, buf);

    if (rc != 0)
        goto out;

    const uint8_t *gd = buf + gd_off % fs->block_size;
    uint64_t table = rd32(gd + 8);

    if (fs->is64 && fs->desc_size >= 64u)
        table |= (uint64_t)rd32(gd + 0x28) << 32;

    uint64_t ino_off = (uint64_t)index * fs->inode_size;

    rc = read_block(fs, table + ino_off / fs->block_size, buf);

    if (rc != 0)
        goto out;

    const uint8_t *in = buf + ino_off % fs->block_size;

    out->ino = ino;
    out->mode = rd16(in + 0);
    out->pad = 0;
    out->flags = rd32(in + 0x20);
    out->size = rd32(in + 4);

    if ((out->mode & EXT4_S_IFMT) == EXT4_S_IFREG)
        out->size |= (uint64_t)rd32(in + 0x6C) << 32;

    copy_bytes(out->block, in + 0x28, 60);

out:
    ext4_free(buf);
    return rc;
}

static int bmap_extents(struct ext4_fs *fs, const struct ext4_inode *inode, uint64_t lblock, uint8_t *scratch, uint64_t *pblock) {
    const uint8_t *node = inode->block;
    uint32_t node_bytes = 60;

    for (uint32_t level = 0; level <= MAX_EXTENT_DEPTH; level++) {
        if (rd16(node) != EXTENT_MAGIC)
            return EXT4_E_FORMAT;

        uint32_t entries = rd16(node + 2);
        uint32_t depth = rd16(node + 6);

        if (12u + entries * 12u > node_bytes || depth > MAX_EXTENT_DEPTH)
            return EXT4_E_FORMAT;

        if (depth == 0) {
            for (uint32_t i = 0; i < entries; i++) {
                const uint8_t *e = node + 12 + i * 12;
                uint64_t first = rd32(e);
                uint32_t len = rd16(e + 4);
                int uninit = len > 32768u;

                if (uninit)
                    len -= 32768u;

                if (lblock >= first && lblock < first + len) {
                    if (uninit)
                        return 1;

                    *pblock = (((uint64_t)rd16(e + 6) << 32) | rd32(e + 8)) + (lblock - first);
                    return 0;
                }
            }

            return 1;
        }

        int found = -1;

        for (uint32_t i = 0; i < entries; i++) {
            if (rd32(node + 12 + i * 12) <= lblock)
                found = (int)i;
        }

        if (found < 0)
            return 1;

        const uint8_t *e = node + 12 + (uint32_t)found * 12;
        uint64_t child = ((uint64_t)rd16(e + 8) << 32) | rd32(e + 4);
        int rc = read_block(fs, child, scratch);

        if (rc != 0)
            return rc;

        node = scratch;
        node_bytes = fs->block_size;
    }

    return EXT4_E_FORMAT;
}

static int bmap_indirect(struct ext4_fs *fs, const struct ext4_inode *inode, uint64_t lblock, uint8_t *scratch, uint64_t *pblock) {
    uint64_t per = fs->block_size / 4u;
    uint64_t cur;

    if (lblock < 12) {
        cur = rd32(inode->block + lblock * 4);
        if (cur == 0)
            return 1;
        *pblock = cur;
        return 0;
    }

    lblock -= 12;

    uint32_t levels;
    uint32_t root;

    if (lblock < per) {
        levels = 1;
        root = 12;
    } else if (lblock - per < per * per) {
        lblock -= per;
        levels = 2;
        root = 13;
    } else if (lblock - per - per * per < per * per * per) {
        lblock -= per + per * per;
        levels = 3;
        root = 14;
    } else {
        return EXT4_E_FORMAT;
    }

    cur = rd32(inode->block + root * 4);

    for (uint32_t l = levels; l > 0; l--) {
        if (cur == 0)
            return 1;

        int rc = read_block(fs, cur, scratch);

        if (rc != 0)
            return rc;

        uint64_t span = 1;

        for (uint32_t k = 1; k < l; k++)
            span *= per;

        cur = rd32(scratch + (lblock / span) * 4);
        lblock %= span;
    }

    if (cur == 0)
        return 1;

    *pblock = cur;
    return 0;
}

static int bmap(struct ext4_fs *fs, const struct ext4_inode *inode, uint64_t lblock, uint8_t *scratch, uint64_t *pblock) {
    if (inode->flags & EXT4_EXTENTS_FL)
        return bmap_extents(fs, inode, lblock, scratch, pblock);

    return bmap_indirect(fs, inode, lblock, scratch, pblock);
}

int64_t ext4_read(struct ext4_fs *fs, const struct ext4_inode *inode, uint64_t offset, void *buf, uint64_t len) {
    if (fs == NULL || inode == NULL || buf == NULL)
        return EXT4_E_FORMAT;

    if (offset >= inode->size)
        return 0;

    if (len > inode->size - offset)
        len = inode->size - offset;

    uint8_t *scratch = (uint8_t *)ext4_alloc(fs->block_size);
    uint8_t *tmp = (uint8_t *)ext4_alloc(fs->block_size);

    if (scratch == NULL || tmp == NULL) {
        ext4_free(scratch);
        ext4_free(tmp);
        return EXT4_E_NOMEM;
    }

    uint8_t *out = (uint8_t *)buf;
    uint64_t done = 0;
    int rc = 0;

    while (done < len) {
        uint64_t pos = offset + done;
        uint64_t lblock = pos / fs->block_size;
        uint32_t boff = (uint32_t)(pos % fs->block_size);
        uint64_t n = fs->block_size - boff;
        uint64_t pblock = 0;

        if (n > len - done)
            n = len - done;

        rc = bmap(fs, inode, lblock, scratch, &pblock);

        if (rc < 0)
            break;

        if (rc == 1) {
            zero_bytes(out + done, n);
            rc = 0;
        } else if (boff == 0 && n == fs->block_size) {
            rc = read_block(fs, pblock, out + done);

            if (rc != 0)
                break;
        } else {
            rc = read_block(fs, pblock, tmp);

            if (rc != 0)
                break;

            copy_bytes(out + done, tmp + boff, n);
        }

        done += n;
    }

    ext4_free(scratch);
    ext4_free(tmp);
    return rc < 0 ? rc : (int64_t)done;
}

static int walk_dir(struct ext4_fs *fs, const struct ext4_inode *dir, const char *name, uint32_t name_len, uint32_t *found_ino, ext4_dir_cb cb, void *cb_ctx) {
    if ((dir->mode & EXT4_S_IFMT) != EXT4_S_IFDIR)
        return EXT4_E_NOTDIR;

    uint8_t *scratch = (uint8_t *)ext4_alloc(fs->block_size);
    uint8_t *blk = (uint8_t *)ext4_alloc(fs->block_size);

    if (scratch == NULL || blk == NULL) {
        ext4_free(scratch);
        ext4_free(blk);
        return EXT4_E_NOMEM;
    }

    uint64_t blocks = (dir->size + fs->block_size - 1) / fs->block_size;
    int rc = EXT4_E_NOTFOUND;

    for (uint64_t lb = 0; lb < blocks && rc == EXT4_E_NOTFOUND; lb++) {
        uint64_t pb = 0;
        int m = bmap(fs, dir, lb, scratch, &pb);

        if (m < 0) {
            rc = m;
            break;
        }

        if (m == 1)
            continue;

        rc = read_block(fs, pb, blk);

        if (rc != 0)
            break;

        rc = EXT4_E_NOTFOUND;

        for (uint32_t off = 0; off + 8u <= fs->block_size;) {
            uint32_t ino = rd32(blk + off);
            uint32_t rec = rd16(blk + off + 4);
            uint32_t nlen = blk[off + 6];
            uint8_t type = blk[off + 7];

            if (rec < 8u || (rec & 3u) != 0 || off + rec > fs->block_size || nlen + 8u > rec) {
                rc = EXT4_E_FORMAT;
                break;
            }

            if (ino != 0) {
                if (cb != NULL) {
                    if (cb(cb_ctx, (const char *)(blk + off + 8), nlen, ino, type) != 0) {
                        rc = 0;
                        break;
                    }
                } else if (nlen == name_len) {
                    uint32_t i = 0;

                    while (i < nlen && blk[off + 8 + i] == (uint8_t)name[i])
                        i++;

                    if (i == nlen) {
                        *found_ino = ino;
                        rc = 0;
                        break;
                    }
                }
            }

            off += rec;
        }
    }

    ext4_free(scratch);
    ext4_free(blk);
    return rc;
}

int ext4_lookup(struct ext4_fs *fs, const struct ext4_inode *dir, const char *name, uint32_t len, uint32_t *ino) {
    if (fs == NULL || dir == NULL || name == NULL || ino == NULL || len == 0 || len > NAME_MAX_LEN)
        return EXT4_E_FORMAT;

    return walk_dir(fs, dir, name, len, ino, NULL, NULL);
}

int ext4_readdir(struct ext4_fs *fs, const struct ext4_inode *dir, ext4_dir_cb cb, void *ctx) {
    if (fs == NULL || dir == NULL || cb == NULL)
        return EXT4_E_FORMAT;

    int rc = walk_dir(fs, dir, NULL, 0, NULL, cb, ctx);

    return rc == EXT4_E_NOTFOUND ? 0 : rc;
}

int ext4_resolve(struct ext4_fs *fs, const char *path, struct ext4_inode *out) {
    if (fs == NULL || path == NULL || out == NULL)
        return EXT4_E_FORMAT;

    int rc = ext4_read_inode(fs, EXT4_ROOT_INO, out);

    for (uint32_t depth = 0; rc == 0 && *path != '\0'; depth++) {
        while (*path == '/')
            path++;

        if (*path == '\0')
            break;

        if (depth >= MAX_PATH_DEPTH)
            return EXT4_E_FORMAT;

        const char *start = path;

        while (*path != '\0' && *path != '/')
            path++;

        uint32_t ino = 0;

        rc = ext4_lookup(fs, out, start, (uint32_t)(path - start), &ino);

        if (rc == 0)
            rc = ext4_read_inode(fs, ino, out);
    }

    return rc;
}
