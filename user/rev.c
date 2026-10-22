#include "ulib.h"

USER_ENTRY {
    char line[128];

    put_str("type a line: ");
    i64 n = read_line(line, sizeof(line));
    if (n < 0)
        u_exit(1);

    for (i64 i = n - 1; i >= 0; i--)
        put_char(line[i]);
    put_char('\n');

    u_exit(0);
}
