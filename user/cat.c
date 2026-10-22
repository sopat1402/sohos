#include "ulib.h"

USER_ENTRY {
    char buf[128];

    for (;;) {
        i64 n = u_read(0, buf, sizeof(buf));
        if (n <= 0)
            break;
        u_write(1, buf, (u64)n);
    }

    u_exit(0);
}
