CC = gcc
LD = ld

CFLAGS = -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Iinclude -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -mno-red-zone -mgeneral-regs-only -MMD -MP
ASFLAGS = -ffreestanding -Iinclude -fno-pie -fno-pic -MMD -MP
LDFLAGS = -n -nostdlib -z max-page-size=0x1000 -z noexecstack -T linker.ld

BUILD = build
KERNEL = $(BUILD)/kernel
ISO = $(BUILD)/sohos.iso
GRUBCFG = iso/boot/grub/grub.cfg

SRCS_C = $(wildcard src/*.c)
SRCS_S = $(wildcard src/*.S)
OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS_C)) $(patsubst src/%.S,$(BUILD)/%.o,$(SRCS_S))
DEPS = $(OBJS:.o=.d)

.PHONY: all clean run debug

all: $(ISO)

$(ISO): $(KERNEL) $(GRUBCFG)
	grub-file --is-x86-multiboot2 $(KERNEL)
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel
	grub-mkrescue -o $@ iso/

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(ISO)
	qemu-system-x86_64 -m 4G -cdrom $(ISO) -no-reboot -no-shutdown

debug: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -no-reboot -no-shutdown -d int,cpu_reset -D $(BUILD)/qemu.log

clean:
	rm -rf $(BUILD) iso/boot/kernel

-include $(DEPS)
