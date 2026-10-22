#ifndef ULIB_H
#define ULIB_H

typedef unsigned long u64;
typedef long i64;

#define SYS_YIELD	24
#define SYS_DEBUG_NUM	500
#define SYS_READ	0
#define SYS_WRITE	1
#define SYS_EXIT	60
#define SYS_WAIT	61
#define SYS_SPAWN	3
#define SYS_CLEAR	700
#define SYS_GETPID 39

#define USER_ENTRY __attribute__((force_align_arg_pointer)) void _start(void)

static inline i64 sys3(i64 n, i64 a, i64 b, i64 c) {
    i64 ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}

static inline i64 u_write(int fd, const void *buf, u64 n) { return sys3(SYS_WRITE, fd, (i64)buf, (i64)n); }
static inline i64 u_read(int fd, void *buf, u64 n) { return sys3(SYS_READ, fd, (i64)buf, (i64)n); }
static inline void u_yield(void) { sys3(SYS_YIELD, 0, 0, 0); }
static inline i64 u_getpid(void) { return sys3(SYS_GETPID, 0, 0, 0); }
static inline i64 u_spawn(const char *path) { return sys3(SYS_SPAWN, (i64)path, 0, 0); }
static inline i64 u_wait(i64 pid, int *status) { return sys3(SYS_WAIT, pid, (i64)status, 0); }
static inline void u_clear(void) { sys3(SYS_CLEAR, 0, 0, 0); }
static inline __attribute__((noreturn)) void u_exit(int code) { sys3(SYS_EXIT, code, 0, 0); for (;;) {} }

static inline u64 str_len(const char *s) {
    u64 n = 0;
    while (s[n])
        n++;
    return n;
}

static inline int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static inline void put_char(char c) { u_write(1, &c, 1); }
static inline void put_str(const char *s) { u_write(1, s, str_len(s)); }

static inline void put_num(i64 v) {
    char tmp[24];
    char out[24];
    int i = 0;
    int j = 0;
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
    while (i > 0)
        out[j++] = tmp[--i];
    u_write(1, out, (u64)j);
}

static inline void put_hex(u64 v) {
    static const char digits[] = "0123456789abcdef";
    char out[16];

    for (int i = 0; i < 16; i++)
        out[i] = digits[(v >> ((15 - i) * 4)) & 0xF];
    u_write(1, "0x", 2);
    u_write(1, out, 16);
}

static char rl_buf[256];
static u64 rl_len;

static inline i64 read_line(char *buf, u64 max) {
    for (;;) {
        u64 line_end = rl_len;
        int found = 0;

        for (u64 i = 0; i < rl_len; i++) {
            if (rl_buf[i] == '\n') {
                line_end = i;
                found = 1;
                break;
            }
        }

        if (found || rl_len == sizeof(rl_buf)) {
            u64 consume = found ? line_end + 1 : rl_len;
            u64 copy = line_end < max - 1 ? line_end : max - 1;

            for (u64 i = 0; i < copy; i++)
                buf[i] = rl_buf[i];
            while (copy > 0 && buf[copy - 1] == '\r')
                copy--;
            buf[copy] = '\0';

            for (u64 i = consume; i < rl_len; i++)
                rl_buf[i - consume] = rl_buf[i];
            rl_len -= consume;
            return (i64)copy;
        }

        i64 n = u_read(0, rl_buf + rl_len, sizeof(rl_buf) - rl_len);

        if (n <= 0) {
            if (rl_len == 0)
                return -1;

            u64 copy = rl_len < max - 1 ? rl_len : max - 1;
            for (u64 i = 0; i < copy; i++)
                buf[i] = rl_buf[i];
            buf[copy] = '\0';
            rl_len = 0;
            return (i64)copy;
        }

        rl_len += (u64)n;
    }
}

static inline const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static inline int parse_num(const char *s, const char **end, i64 *out) {
    int neg = 0;
    i64 v = 0;
    int digits = 0;

    s = skip_spaces(s);
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        digits++;
        s++;
    }
    if (!digits)
        return 0;
    *out = neg ? -v : v;
    *end = s;
    return 1;
}

#endif
