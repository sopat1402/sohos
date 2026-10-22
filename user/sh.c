#include "ulib.h"

#define LINE_MAX 128
#define PATH_MAX 64

static char *next_word(char *s, char **rest) {
    while (*s == ' ' || *s == '\t')
        s++;

    if (*s == '\0') {
        *rest = s;
        return 0;
    }

    char *word = s;

    while (*s != '\0' && *s != ' ' && *s != '\t')
        s++;

    if (*s != '\0') {
        *s = '\0';
        s++;
    }

    *rest = s;
    return word;
}

static int has_slash(const char *s) {
    while (*s) {
        if (*s == '/')
            return 1;
        s++;
    }
    return 0;
}

static int build_path(char *out, u64 cap, const char *cmd) {
    const char *prefix = has_slash(cmd) ? "" : "/bin/";
    u64 n = 0;

    for (u64 i = 0; prefix[i] != '\0'; i++) {
        if (n + 1 >= cap)
            return -1;
        out[n++] = prefix[i];
    }

    for (u64 i = 0; cmd[i] != '\0'; i++) {
        if (n + 1 >= cap)
            return -1;
        out[n++] = cmd[i];
    }

    out[n] = '\0';
    return 0;
}

static void print_help(void) {
    put_str("builtins: help clear echo pid status exit\n");
    put_str("programs: greet cat rev upper wc count fib primes calc guess\n");
    put_str("          segv divzero stackhog (these three are crash tests)\n");
    put_str("anything else is run from /bin, or give a path like /bin/fib\n");
}

static void run_program(const char *cmd, const char *rest, int *last_status) {
    char path[PATH_MAX];

    if (*skip_spaces(rest) != '\0')
        put_str("sh: note: arguments are not passed to programs yet\n");

    if (build_path(path, sizeof(path), cmd) < 0) {
        put_str("sh: command too long\n");
        return;
    }

    i64 pid = u_spawn(path);
    if (pid < 0) {
        put_str("sh: command not found: ");
        put_str(cmd);
        put_char('\n');
        *last_status = 127;
        return;
    }

    int status = 0;
    i64 reaped = u_wait(pid, &status);

    if (reaped < 0) {
        put_str("sh: wait failed\n");
        return;
    }

    *last_status = status;

    if (status != 0) {
        put_str("[exit status ");
        put_num(status);
        put_str("]\n");
    }
}

USER_ENTRY {
    char line[LINE_MAX];
    int last_status = 0;

    put_str("SOHOS shell. Type help for commands.\n");

    for (;;) {
        put_str("sohos:/$ ");

        i64 n = read_line(line, sizeof(line));

        if (n < 0) {
            put_char('\n');
            break;
        }

        char *rest;
        char *cmd = next_word(line, &rest);

        if (cmd == 0)
            continue;

        if (str_eq(cmd, "help")) {
            print_help();
        } else if (str_eq(cmd, "clear")) {
            u_clear();
        } else if (str_eq(cmd, "echo")) {
            put_str(skip_spaces(rest));
            put_char('\n');
        } else if (str_eq(cmd, "pid")) {
            put_num(u_getpid());
            put_char('\n');
        } else if (str_eq(cmd, "status")) {
            put_num(last_status);
            put_char('\n');
        } else if (str_eq(cmd, "exit")) {
            const char *end;
            i64 code = last_status;
            if (!parse_num(rest, &end, &code))
                code = last_status;
            u_exit((int)code);
        } else {
            run_program(cmd, rest, &last_status);
        }
    }

    u_exit(0);
}
