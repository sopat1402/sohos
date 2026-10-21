#ifndef KBD_TEST_H
#define KBD_TEST_H

#include <stdint.h>
#include "../include/display.h"
#include "../include/process.h"

extern const uint8_t user_exit_start[];
extern const uint8_t user_exit_end[];

static void kbd_test_thread(void *arg) {
    (void)arg;
    print("Starting interactive keyboard test. Type on your keyboard and hit Enter!\n");
    process_create("kbd_test", user_exit_start, (uint64_t)(user_exit_end - user_exit_start));
}

#endif
