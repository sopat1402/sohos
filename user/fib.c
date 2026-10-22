#include "ulib.h"

USER_ENTRY {
    u64 a = 0;
    u64 b = 1;

    for (int i = 0; i < 25; i++) {
        put_num((i64)a);
        put_char(i % 5 == 4 ? '\n' : ' ');
        u64 next = a + b;
        a = b;
        b = next;
    }

    u_exit(0);
}
