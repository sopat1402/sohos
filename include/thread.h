#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#define THREAD_STACK_ORDER 3u
#define THREAD_NAME_LEN 16u
#define THREAD_CANARY 0x5AFEC0DEDEADBEEFull

enum thread_state {
    THREAD_NEW = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
};

typedef void (*thread_fn)(void *arg);

struct thread {
    uint64_t saved_rsp;
    uint64_t stack_phys;
    uint8_t *stack_base;
    uint64_t stack_size;
    struct thread *next;
    struct thread *prev;
    uint32_t id;
    uint32_t state;
    uint32_t cpu;
    uint32_t slice_left;
    uint32_t on_cpu;
    int32_t exit_code;
    uint8_t wake_pending;
    char name[THREAD_NAME_LEN];
};

struct thread *thread_create(const char *name, thread_fn fn, void *arg);
struct thread *thread_create_on(const char *name, thread_fn fn, void *arg, uint32_t cpu);
__attribute__((noreturn)) void thread_exit(void);
struct thread *thread_current(void);
uint32_t thread_alloc_id(void);
void thread_set_name(struct thread *t, const char *name);
void thread_free(struct thread *t);
void set_tss_rsp0_currthrd();

#endif
