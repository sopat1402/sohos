#include "ulib.h"

USER_ENTRY {
    volatile int a = 1;
    volatile int b = 0;

    put_str("divzero: dividing by zero, the thread should be killed\n");
    put_num(a / b);
    put_str("divzero: this line should never print\n");
    u_exit(0);
}
