#include "ulib.h"

USER_ENTRY {
    put_str("segv: writing to address 0, the thread should be killed\n");
    *(volatile int *)0 = 1;
    put_str("segv: this line should never print\n");
    u_exit(0);
}
