#ifndef SPNLCK
#define SPNLCK

#include <stdatomic.h>

#define SPINLOCK_INIT { ATOMIC_FLAG_INIT }

typedef struct{
	atomic_flag flag;
} spinlock_t;

static inline void spin_lock(spinlock_t *lock){
    while (atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire)) {
        __asm__ volatile("pause");
    }
}

static inline void spin_unlock(spinlock_t *lock){
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

#endif
