#include "kernel.h"

void spinlock_lock(Spinlock *lock) {
    while (__atomic_exchange_n(&lock->value, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->value, __ATOMIC_RELAXED)) {
            __asm__ volatile("pause");
        }
    }
}

void spinlock_unlock(Spinlock *lock) {
    __atomic_store_n(&lock->value, 0, __ATOMIC_RELEASE);
}
