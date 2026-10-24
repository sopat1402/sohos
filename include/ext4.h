#ifndef EXT4_H
#define EXT4_H

#include <stdint.h>

#define EXT4_E_IO          (-1)
#define EXT4_E_FORMAT      (-2)
#define EXT4_E_NOTFOUND    (-3)
#define EXT4_E_NOTDIR      (-4)
#define EXT4_E_NOMEM       (-5)
#define EXT4_E_UNSUPPORTED (-6)

#define EXT4_ROOT_INO 2u

#define EXT4_S_IFMT   0xF000u
#define EXT4_S_IFREG  0x8000u
#define EXT4_S_IFDIR  0x4000u
#define EXT4_S_IFLNK  0xA000u

#define EXT4_FT_REG_FILE 1u
#define EXT4_FT_DIR      2u
#define EXT4_FT_SYMLINK  7u

typedef int (*ext4_read_fn)(void *ctx, uint64_t sector, void *buf, uint32_t count);

struct ext4_fs {
    ext4_read_fn read;
    void *ctx;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t first_data_block;
    uint32_t groups;
    uint64_t blocks_count;
    int is64;
};

struct ext4_inode {
    uint32_t ino;
    uint16_t mode;
    uint16_t pad;
    uint32_t flags;
    uint64_t size;
    uint8_t block[60];
};

typedef int (*ext4_dir_cb)(void *ctx, const char *name, uint32_t len, uint32_t ino, uint8_t type);

int ext4_mount(struct ext4_fs *fs, ext4_read_fn read, void *ctx);
int ext4_read_inode(struct ext4_fs *fs, uint32_t ino, struct ext4_inode *out);
int ext4_lookup(struct ext4_fs *fs, const struct ext4_inode *dir, const char *name, uint32_t len, uint32_t *ino);
int ext4_resolve(struct ext4_fs *fs, const char *path, struct ext4_inode *out);
int64_t ext4_read(struct ext4_fs *fs, const struct ext4_inode *inode, uint64_t offset, void *buf, uint64_t len);
int ext4_readdir(struct ext4_fs *fs, const struct ext4_inode *dir, ext4_dir_cb cb, void *ctx);

#endif
