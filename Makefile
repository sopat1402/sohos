CC = gcc
LD = ld
QEMU = qemu-system-x86_64

BUILD_DIR = build
KERNEL = $(BUILD_DIR)/kernel.elf
ISO = $(BUILD_DIR)/sohos.iso
GRUBCFG = iso/boot/grub/grub.cfg

CFLAGS = \
	-g \
	-std=gnu11 \
	-ffreestanding \
	-O2 \
	-Wall -Wextra \
	-Iinclude \
	-fno-pie -fno-pic \
	-fno-stack-protector \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-fno-tree-loop-distribute-patterns \
	-m64 -mcmodel=kernel \
	-mno-red-zone \
	-mgeneral-regs-only \
	-MMD -MP

ASFLAGS = \
	-ffreestanding \
	-Iinclude \
	-fno-pie -fno-pic \
	-m64 \
	-MMD -MP

LDFLAGS = \
	-m elf_x86_64 \
	-nostdlib \
	-z max-page-size=0x1000 \
	-z noexecstack \
	--build-id=none \
	-T linker.ld

C_SOURCES = $(wildcard src/*.c)
ASM_SOURCES = $(wildcard src/*.S)

OBJECTS = \
	$(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
	$(patsubst src/%.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

DEPFILES = $(OBJECTS:.o=.d)

.PHONY: all kernel iso run debug clean gdb

# Keep the existing default behavior: build the bootable ISO.
all: iso

kernel: $(KERNEL)

iso: $(ISO)

$(KERNEL): $(OBJECTS) linker.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/initrd.tar: $(USER_BINS) | $(BUILD_DIR)
	tar --format=ustar -cf $@ -C rootfs .

$(ISO): $(KERNEL) $(GRUBCFG) $(BUILD_DIR)/initrd.tar
	grub-file --is-x86-multiboot2 $(KERNEL)
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel
	cp $(BUILD_DIR)/initrd.tar iso/boot/initrd.tar
	grub-mkrescue -o $@ iso/

run: $(ISO)
	$(QEMU) -cpu qemu64,+smep,+smap -m 4G -smp 4 -cdrom $(ISO) -no-reboot -no-shutdown

debug: $(ISO)
	$(QEMU) -cpu qemu64,+smep,+smap -m 4G -smp 4 -cdrom $(ISO) -no-reboot -no-shutdown \
		-s -d int,cpu_reset -D $(BUILD_DIR)/qemu.log

gdb: $(KERNEL)
	gdb $(KERNEL) \
		-ex 'target remote :1234'

clean:
	rm -rf $(BUILD_DIR) rootfs
	rm -f iso/boot/kernel iso/boot/initrd.tar

USER_CFLAGS = -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -mgeneral-regs-only
USER_SRCS := $(wildcard user/*.c)
USER_BINS := $(patsubst user/%.c,rootfs/bin/%,$(USER_SRCS))

rootfs/bin/%: user/%.c user/ulib.h
	mkdir -p rootfs/bin build/user
	gcc $(USER_CFLAGS) -c $< -o build/user/$*.o
	ld -static -nostdlib -z max-page-size=4096 -z separate-code -Ttext-segment=0x400000 -e _start -o $@ build/user/$*.o

build/initrd.tar: $(USER_BINS)
	tar --format=ustar -cf $@ -C rootfs .

-include $(DEPFILES)
