#include "kernel.h"

void panic(const char *message) {
    kprintf("PANIC: %s\n", message);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
