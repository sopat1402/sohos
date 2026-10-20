#ifndef LEAK_TEST_H
#define LEAK_TEST_H

#include <stdint.h>
#include "../include/buddy.h"
#include "../include/display.h"
#include "../include/process.h"
#include "../include/thread.h"
#include "../include/legacy.h"

#define LEAK_RUNS     100u
#define LEAK_WAIT_MS  50u
#define LEAK_SLACK    16u

extern const uint8_t user_exit_start[];
extern const uint8_t user_exit_end[];

static void leak_run_one(void) {
    process_create("leak", user_exit_start, (uint64_t)(user_exit_end - user_exit_start));

    for (uint32_t i = 0; i < LEAK_WAIT_MS; i++)
        pit_delay_us(1000);
}

static void leak_test_thread(void *arg) {
    (void)arg;

    leak_run_one();

    uint64_t before = buddy_available_pages();

    for (uint32_t i = 0; i < LEAK_RUNS; i++)
        leak_run_one();

    for (uint32_t i = 0; i < 100; i++)
        pit_delay_us(1000);

    uint64_t after = buddy_available_pages();

    print("leak test: before=");
    print_uint(before);
    print(" after=");
    print_uint(after);
    print((after >= before || before - after <= LEAK_SLACK) ? " OK\n" : " LEAK\n");
}

#endif
