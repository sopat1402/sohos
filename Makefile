CC = gcc
LD = ld
QEMU = qemu-system-x86_64

BUILD_DIR = build
KERNEL = $(BUILD_DIR)/kernel.elf
ISO = $(BUILD_DIR)/sohos.iso
GRUBCFG = iso/boot/grub/grub.cfg

CFLAGS = \
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

.PHONY: all kernel iso run debug clean

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

$(ISO): $(KERNEL) $(GRUBCFG)
	grub-file --is-x86-multiboot2 $(KERNEL)
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel
	grub-mkrescue -o $@ iso/

run: $(ISO)
	$(QEMU) -m 4G -cdrom $(ISO) -no-reboot -no-shutdown

debug: $(ISO)
	$(QEMU) -cdrom $(ISO) -no-reboot \
		-d int,cpu_reset \
		-D $(BUILD_DIR)/qemu.log

clean:
	rm -rf $(BUILD_DIR)
	rm -f iso/boot/kernel

-include $(DEPFILES)
