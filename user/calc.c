#include "ulib.h"

USER_ENTRY {
    char line[64];

    put_str("calc: type  a + b  (ops: + - * /), q quits\n");

    for (;;) {
        put_str("> ");
        i64 n = read_line(line, sizeof(line));
        if (n < 0 || line[0] == 'q')
            break;

        const char *p = line;
        i64 a;
        i64 b;

        if (!parse_num(p, &p, &a)) {
            put_str("bad input\n");
            continue;
        }

        p = skip_spaces(p);
        char op = *p;
        if (op == '\0') {
            put_str("bad input\n");
            continue;
        }
        p++;

        if (!parse_num(p, &p, &b)) {
            put_str("bad input\n");
            continue;
        }

        i64 r;
        if (op == '+') {
            r = a + b;
        } else if (op == '-') {
            r = a - b;
        } else if (op == '*') {
            r = a * b;
        } else if (op == '/') {
            if (b == 0) {
                put_str("division by zero\n");
                continue;
            }
            r = a / b;
        } else {
            put_str("unknown operator\n");
            continue;
        }

        put_num(r);
        put_char('\n');
    }

    u_exit(0);
}
