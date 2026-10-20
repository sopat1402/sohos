#ifndef USER_TEST_H
#define USER_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../include/display.h"

#define SYS_READ  0
#define SYS_WRITE 1

static inline int64_t u_write(int fd, const void *buf, size_t count) {
	int64_t ret;
	__asm__ volatile (
		"syscall"
		: "=a"(ret)
		: "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
		: "rcx", "r11", "memory"
	);
	return ret;
}

static inline int64_t u_read(int fd, void *buf, size_t count) {
	int64_t ret;
	__asm__ volatile (
		"syscall"
		: "=a"(ret)
		: "a"(SYS_READ), "D"(fd), "S"(buf), "d"(count)
		: "rcx", "r11", "memory"
	);
	return ret;
}

static inline void run_sys_test(void) {
	print("Entered the test fn\n");
	const char *msg = "Hello from hardened user space!\n";
	u_write(1, msg, 31);
	print("write done\n");
	char read_buf[32] = {0};
	int64_t bytes_read = u_read(0, read_buf, sizeof(read_buf) - 1);
	print("read done\n");
	if (bytes_read > 0) {
		u_write(1, "Read back: ", 11);
		u_write(1, read_buf, bytes_read);
	}
}

#endif
