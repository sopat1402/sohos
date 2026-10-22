#include "ulib.h"

#define SECRET 37
#define MAX_TRIES 8

USER_ENTRY {
    char line[32];

    put_str("I am thinking of a number from 1 to 100. You have 8 tries.\n");

    for (int tries = 1; tries <= MAX_TRIES; tries++) {
        put_str("guess: ");
        i64 n = read_line(line, sizeof(line));
        if (n < 0)
            u_exit(1);

        const char *end;
        i64 g;
        if (!parse_num(line, &end, &g)) {
            put_str("numbers only\n");
            tries--;
            continue;
        }

        if (g == SECRET) {
            put_str("correct, in ");
            put_num(tries);
            put_str(" tries\n");
            u_exit(0);
        }
        put_str(g < SECRET ? "higher\n" : "lower\n");
    }

    put_str("out of tries\n");
    u_exit(0);
}
