#include <stdint.h>
#include <stddef.h>
#include "../include/usercpy.h"
#include "../include/display.h"

#define USER_ADDR_MAX 0x0000800000000000ULL

extern struct ex_table_entry __start___ex_table[];
extern struct ex_table_entry __stop___ex_table[];

uint64_t search_exception_tables(uint64_t fault_rip) {
	for (struct ex_table_entry *entry = __start___ex_table; entry < __stop___ex_table; entry++) {
		uint64_t insn_addr = (uint64_t)&entry->insn_offset + entry->insn_offset;
		if (insn_addr == fault_rip) {
			return (uint64_t)&entry->fixup_offset + entry->fixup_offset;
		}
	}
	return 0;
}

inline int access_ok(const void *addr, size_t size) {
	uint64_t start = (uint64_t)addr;
	uint64_t end = start + size;
	if (end < start || end > USER_ADDR_MAX) {
		return 0;
	}
	return 1;
}

#define _ASM_EXTABLE(from, to)                  \
	" .pushsection .ex_table, \"a\"\n\t"        \
	" .align 4\n\t"                             \
	" .long (" #from " - .)\n\t"                \
	" .long (" #to " - .)\n\t"                  \
	" .popsection\n\t"

unsigned long copy_from_user(void *to, const void *from, unsigned long n) {
	if (!access_ok(from, n)) {
		return n;
	}

	unsigned long res = n;
	__asm__ volatile (
		"stac\n\t"
		"1:\n\t"
		"rep movsb\n\t"
		"clac\n\t"
		"jmp 3f\n\t"
		"2:\n\t"
		"clac\n\t"
		"3:\n\t"
		_ASM_EXTABLE(1b, 2b)
		: "+D"(to), "+S"(from), "+c"(res)
		:
		: "memory", "cc"
	);
	return res;
}

unsigned long copy_to_user(void *to, const void *from, unsigned long n) {
	if (!access_ok(to, n)) {
		return n;
	}

	unsigned long res = n;
	__asm__ volatile (
		"stac\n\t"
		"1:\n\t"
		"rep movsb\n\t"
		"clac\n\t"
		"jmp 3f\n\t"
		"2:\n\t"
		"clac\n\t"
		"3:\n\t"
		_ASM_EXTABLE(1b, 2b)
		: "+D"(to), "+S"(from), "+c"(res)
		:
		: "memory", "cc"
	);
	return res;
}

void enable_smep_smap(void) {
	uint32_t eax, ebx, ecx, edx;

	__asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
	if (eax < 7) {
		print("[KERNEL] CPUID leaf 7 not supported; skipping SMEP/SMAP.\n");
		return;
	}

	__asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));

	int has_smep = (ebx & (1 << 7)) != 0;
	int has_smap = (ebx & (1 << 20)) != 0;

	uint64_t cr4;
	__asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

	if (has_smep) {
		cr4 |= (1ULL << 20); // Enable SMEP
	} else {
		print("[KERNEL] SMEP NOT supported by CPU.\n");
	}

	if (has_smap) {
		cr4 |= (1ULL << 21); // Enable SMAP
	} else {
		print("[KERNEL] SMAP NOT supported by CPU.\n");
	}

	__asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
}
