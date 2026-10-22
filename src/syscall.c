#include <stdint.h>
#include <stddef.h>
#include "../include/syscall.h"
#include "../include/idt.h"
#include "../include/sched.h"
#include "../include/display.h"
#include "../include/usercpy.h"
#include "../include/process.h"
#include "../include/ps2.h"

#define SC_MSR_EFER 0xC0000080u
#define SC_MSR_STAR 0xC0000081u
#define SC_MSR_LSTAR 0xC0000082u
#define SC_MSR_FMASK 0xC0000084u
#define SC_EFER_SCE 1ull
#define SC_FMASK 0x47700ull
#define EBADF 9

extern void syscall_entry(void);

static uint64_t sc_rdmsr(uint32_t msr) {
	uint32_t low;
	uint32_t high;
	__asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
	return ((uint64_t)high << 32) | low;
}

static void sc_wrmsr(uint32_t msr, uint64_t value) {
	uint32_t low = (uint32_t)value;
	uint32_t high = (uint32_t)(value >> 32);
	__asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

void syscall_init(void) {
	sc_wrmsr(SC_MSR_EFER, sc_rdmsr(SC_MSR_EFER) | SC_EFER_SCE);
	sc_wrmsr(SC_MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
	sc_wrmsr(SC_MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
	sc_wrmsr(SC_MSR_FMASK, SC_FMASK);
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
	if (fd != 1 && fd != 2) {
		return -1;
	}
	char stack_buf[256];
	size_t total_written = 0;
	while (total_written < count) {
		size_t chunk = count - total_written;
		if (chunk > sizeof(stack_buf)) {
			chunk = sizeof(stack_buf);
		}
		unsigned long uncopied = copy_from_user(stack_buf, (const char *)buf + total_written, chunk);
		size_t copied = chunk - uncopied;
		if (copied == 0) {
			return -1;
		}
		for (size_t i = 0; i < copied; i++) {
			char tmp[2] = { stack_buf[i], '\0' };
			print(tmp);
		}
		total_written += copied;
		if (uncopied > 0) {
			break;
		}
	}

	return (int64_t)total_written;
}

long sys_read(int fd, char *buf, unsigned long count) {
	if (fd!=0){
		return -EBADF;
	}
	unsigned long read=kbd_read(count,buf);
	return read;
}

int32_t sys_wait(int32_t pid, int *status_user) {
    int status = 0;
    int32_t r = process_wait(pid, &status);

    if (r >= 0 && status_user != NULL && copy_to_user(status_user, &status, sizeof(status)) != 0)
        return -14;

    return r;
}

void syscall_dispatch(struct interrupt_frame *f) {
	switch (f->rax) {
	case SYS_EXIT:
		sys_exit((int)f->rdi);
		break;
	case SYS_WAIT:
		f->rax = (uint64_t)(int64_t)sys_wait((int32_t)f->rdi, (int *)f->rsi);
		break;
	case SYS_SPAWN: {
	    const char *user_path = (const char *)f->rdi;
	    f->rax = (uint64_t)sys_spawn(user_path);
	    break;
	}
	case SYS_YIELD:
		schedule();
		f->rax = 0;
		break;
	case SYS_CLEAR:
	    display_clear();
	    f->rax = 0;
	    break;
	case SYS_DEBUG_NUM:
		print("[SYSCALL] ");
		print_uint(f->rdi);
		print("\n");
		f->rax = 0;
		break;
	case SYS_READ:
		f->rax = (uint64_t)sys_read((int)f->rdi, (void *)f->rsi, (size_t)f->rdx);
		break;
	case SYS_WRITE:
		f->rax = (uint64_t)sys_write((int)f->rdi, (const void *)f->rsi, (size_t)f->rdx);
		break;
	default:
		f->rax = (uint64_t)-38;
		break;
	}
}
