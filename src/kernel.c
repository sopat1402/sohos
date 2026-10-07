#include <stdint.h>

static volatile uint8_t *vga = (volatile uint8_t *)0xB8000;
static uint16_t cursor = 0;

struct multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct multiboot_mmap_tag {
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
};

struct multiboot_mmap_entry {
	uint64_t base_addr;
	uint64_t length;
	uint32_t type;
	uint32_t reserved;
};

void putchar(char c){
	if (cursor >= 80 * 25)
		return;
	if (c == '\n') {
		cursor += 80 - (cursor % 80);
		return;
	}
	vga[cursor * 2] = c;
	vga[cursor * 2 + 1] = 0x07;
	cursor++;
}

void print(const char *str){
    while (*str)
        putchar(*str++);
}

void print_hex(uint64_t value) {
    char buffer[17];
    int i = 16;
    buffer[16] = '\0';
    do {
        uint8_t digit = value & 0xF;
        buffer[--i] = digit < 10
            ? '0' + digit
            : 'A' + (digit - 10);
        value >>= 4;
    } while (value != 0);
    print(&buffer[i]);
}

void print_uint(uint64_t value){
	char buffer[21];
	int i = 20;
	buffer[i] = '\0';
	do {
		buffer[--i] = '0' + (value % 10);
		value /= 10;
	} while (value != 0);
	print(&buffer[i]);
}

void print_size(uint64_t value){
	char *units[]={"B","KB","MB","GB","TB","PB"};
	int i=0;
	while (value>=1024 && i<5){
		value/=1024;
		i++;
	}
	print_uint(value);
	print(" ");
	print(units[i]);
}

void parse_multiboot(uint64_t multiboot_data) {
    struct multiboot_info *info =
        (struct multiboot_info *)(uintptr_t)multiboot_data;
    if (info->total_size < 16) {
        print("Invalid Multiboot info block\n");
        return;
    }
    print("Multiboot info size: ");
    print_uint(info->total_size);
    print(" bytes\n");
    print("Multiboot reserved field: ");
    print_uint(info->reserved);
    print("\n\n");
    uint8_t *info_start = (uint8_t *)info;
    uint8_t *info_end = info_start + info->total_size;
    uint8_t *tag_ptr = info_start + 8;
    uint64_t total_available = 0;
    while ((uint64_t)(info_end - tag_ptr) >= 8) {
        struct multiboot_tag *tag =
            (struct multiboot_tag *)tag_ptr;
        if (tag->size < 8 ||
            (uint64_t)(info_end - tag_ptr) < tag->size) {
            print("Invalid Multiboot tag\n");
            break;
        }
        if (tag->type == 0)
            break;
        if (tag->type == 6 && tag->size >= 16) {
            struct multiboot_mmap_tag *mmap =
                (struct multiboot_mmap_tag *)tag;
            print("Memory map:\n");
            if (mmap->entry_size < sizeof(struct multiboot_mmap_entry)) {
                print("Invalid memory-map entry size\n");
            } else {
                uint32_t offset = 16;
                while (offset <= tag->size &&
                       mmap->entry_size <= tag->size - offset) {
                    struct multiboot_mmap_entry *entry =
                        (struct multiboot_mmap_entry *)(tag_ptr + offset);
                    uint64_t end_addr = entry->base_addr + entry->length;
                    switch (entry->type) {
                        case 1:
                            print("Available RAM");
                            total_available += entry->length;
                            break;
                        case 2:
                            print("Reserved");
                            break;
                        case 3:
                            print("ACPI reclaimable");
                            break;
                        case 4:
                            print("ACPI NVS");
                            break;
                        case 5:
                            print("Bad RAM");
                            break;
                        default:
                            print("Unknown type ");
                            print_uint(entry->type);
                            break;
                    }
                    print(": [0x");
                    print_hex(entry->base_addr);
                    print(", 0x");
                    print_hex(end_addr);
                    print(")  length ");
                    print_size(entry->length);
                    print(" (");
                    print_uint(entry->length);
                    print(" bytes)\n");
                    offset += mmap->entry_size;
                }
            }
        }
        uint64_t aligned_size =
            ((uint64_t)tag->size + 7u) & ~7ull;
        if (aligned_size > (uint64_t)(info_end - tag_ptr))
            break;
        tag_ptr += aligned_size;
    }
    print("\nTotal available RAM in type-1 entries: ");
    print_size(total_available);
    print(" (");
    print_uint(total_available);
    print(" bytes)\n");
}

void kmain(uint32_t magic, uint64_t multiboot_data){
	parse_multiboot(multiboot_data);
}
