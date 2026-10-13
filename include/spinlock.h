#ifndef SPNLCK
#define SPNLCK

#include <stdatomic.h>
#include <stdint.h>

#define SPINLOCK_INIT { ATOMIC_FLAG_INIT }
#define RFLAGS_IF 0x200ull

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

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & RFLAGS_IF)
        __asm__ volatile("sti" : : : "memory");
}

static inline uint64_t spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spin_unlock(lock);
    irq_restore(flags);
}

#endif
