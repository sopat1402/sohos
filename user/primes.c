#include "ulib.h"

#define LIMIT 200

USER_ENTRY {
    char composite[LIMIT + 1];
    int printed = 0;

    for (int i = 0; i <= LIMIT; i++)
        composite[i] = 0;

    for (int i = 2; i <= LIMIT; i++) {
        if (composite[i])
            continue;
        put_num(i);
        put_char(++printed % 10 == 0 ? '\n' : ' ');
        for (int j = i * 2; j <= LIMIT; j += i)
            composite[j] = 1;
    }

    put_char('\n');
    u_exit(0);
}
