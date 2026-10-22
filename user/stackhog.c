#include "ulib.h"

static int deep(int n) {
    volatile char pad[512];

    pad[0] = (char)n;
    if ((n & 3) == 0) {
        put_str("depth ");
        put_num(n);
        put_char('\n');
    }
    return deep(n + 1) + pad[0];
}

USER_ENTRY {
    put_str("stackhog: recursing until the user stack runs out\n");
    put_num(deep(0));
    u_exit(0);
}
