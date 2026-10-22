#include "ulib.h"

USER_ENTRY {
    for (int i = 1; i <= 10; i++) {
        put_num(i);
        put_char('\n');
        for (int k = 0; k < 20; k++)
            u_yield();
    }

    u_exit(0);
}
