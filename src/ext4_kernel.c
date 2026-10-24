#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"
#include "../include/block.h"
#include "../include/ext4.h"
#include "../include/ext4_kernel.h"

static int block_cb(void *ctx, uint64_t sector, void *buf, uint32_t count) {
    return block_read((struct block_device *)ctx, sector, buf, count);
}

int ext4_mount_vda(struct ext4_fs *fs) {
    struct block_device *dev = block_find("vda");

    if (dev == NULL)
        return -1;

    return ext4_mount(fs, block_cb, dev);
}

static int print_entry(void *ctx, const char *name, uint32_t len, uint32_t ino, uint8_t type) {
    (void)ctx;
    (void)ino;

    for (uint32_t i = 0; i < len; i++)
        putchar(name[i]);

    print(type == EXT4_FT_DIR ? "/ " : " ");
    return 0;
}

void ext4_test(void) {
    static struct ext4_fs fs;
    int rc = ext4_mount_vda(&fs);

    print("ext4 mount ");
    print_uint((uint64_t)(rc < 0 ? -rc : rc));
    print(" blocksize ");
    print_uint(fs.block_size);
    print(" groups ");
    print_uint(fs.groups);
    print("\n");

    if (rc != 0)
        return;

    struct ext4_inode root;

    if (ext4_resolve(&fs, "/", &root) == 0) {
        print("/ : ");
        (void)ext4_readdir(&fs, &root, print_entry, NULL);
        print("\n");
    }

    struct ext4_inode sh;
    uint8_t head[4] = {0, 0, 0, 0};

    if (ext4_resolve(&fs, "/bin/sh", &sh) != 0) {
        print("/bin/sh not found\n");
        return;
    }

    int64_t n = ext4_read(&fs, &sh, 0, head, 4);

    print("/bin/sh size ");
    print_uint(sh.size);
    print(" read ");
    print_uint((uint64_t)(n < 0 ? 0 : n));
    print(" elf ");
    print_uint(n == 4 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F');
    print("\n");
}
