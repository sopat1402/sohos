CC = gcc
AS = as
LD = ld

CFLAGS = -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -T linker.ld

KERNEL = build/kernel
ISO = build/sohos.iso

OBJS = build/entry.o build/kernel.o

all: $(ISO)

$(ISO): $(KERNEL)
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel
	grub-mkrescue -o $@ iso/

$(KERNEL): $(OBJS)
	mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $^

build/kernel.o: src/kernel.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/entry.o: src/entry.S
	mkdir -p build
	$(AS) $< -o $@

clean:
	rm -rf build/*
	rm -rf iso/boot/kernel

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO)
