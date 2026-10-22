#include "ulib.h"

USER_ENTRY {
    char line[128];

    for (;;) {
        i64 n = read_line(line, sizeof(line));
        if (n < 0)
            break;
        for (i64 i = 0; i < n; i++) {
            char c = line[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            put_char(c);
        }
        put_char('\n');
    }

    u_exit(0);
}
