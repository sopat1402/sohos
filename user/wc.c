#include "ulib.h"

USER_ENTRY {
    char buf[128];
    u64 lines = 0;
    u64 words = 0;
    u64 bytes = 0;
    int in_word = 0;

    for (;;) {
        i64 n = u_read(0, buf, sizeof(buf));
        if (n <= 0)
            break;
        for (i64 i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n')
                lines++;
            if (c == ' ' || c == '\t' || c == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    put_num((i64)lines);
    put_str(" lines, ");
    put_num((i64)words);
    put_str(" words, ");
    put_num((i64)bytes);
    put_str(" bytes\n");
    u_exit(0);
}
