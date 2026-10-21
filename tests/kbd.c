typedef unsigned long u64;
typedef long i64;

static inline i64 sys3(i64 n, i64 a, i64 b, i64 c) {
    i64 ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}

static i64 u_write(int fd, const void *buf, u64 n) { return sys3(1, fd, (i64)buf, (i64)n); }
static i64 u_read(int fd, void *buf, u64 n) { return sys3(0, fd, (i64)buf, (i64)n); }
static void u_exit(int code) { sys3(60, code, 0, 0); for (;;) {} }

static u64 str_len(const char *s) {
    u64 n = 0;
    while (s[n])
        n++;
    return n;
}

static void put_str(const char *s) {
    u_write(1, s, str_len(s));
}

static void put_num(i64 v) {
    char tmp[24];
    int i = 0;
    int neg = v < 0;
    u64 u = neg ? (u64)(-v) : (u64)v;

    if (u == 0)
        tmp[i++] = '0';
    while (u != 0) {
        tmp[i++] = (char)('0' + (u % 10));
        u /= 10;
    }
    if (neg)
        tmp[i++] = '-';

    char out[24];
    int j = 0;
    while (i > 0)
        out[j++] = tmp[--i];
    out[j++] = '\n';
    u_write(1, out, (u64)j);
}

__attribute__((force_align_arg_pointer)) void _start(void) {
    char buf[32];

    put_str("1. write ok\n");

    put_str("2. read fd 0: ");
    i64 n = u_read(0, buf, sizeof(buf) - 1);
    if (n > 0) {
        u_write(1, buf, (u64)n);
    } else {
        put_num(n);
    }

    put_str("3. write from unmapped user ptr (expect -14): ");
    put_num(u_write(1, (const void *)0x10000, 4));

    put_str("4. write from kernel ptr (expect -14): ");
    put_num(u_write(1, (const void *)0xFFFF800000000000UL, 4));

    put_str("5. read into unmapped user ptr (expect -14): ");
    put_num(u_read(0, (void *)0x10000, 4));

    put_str("6. read into kernel ptr (expect -14): ");
    put_num(u_read(0, (void *)0xFFFF800000000000UL, 4));

    put_str("done\n");
    u_exit(0);
}
