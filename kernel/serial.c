#include "kernel.h"
#include <stdarg.h>

#define COM1 0x3f8

static Spinlock serial_lock;

static uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

static int serial_ready(void) {
    return inb(COM1 + 5) & 0x20;
}

int serial_read_byte(void) {
    if ((inb(COM1 + 5) & 0x01) == 0) return -1;
    return inb(COM1);
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while (!serial_ready()) {}
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    while (*s) serial_putc(*s++);
}

static void print_uint(uint64_t v, unsigned base) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    int i = 0;
    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v && i < (int)sizeof(buf)) {
        buf[i++] = digits[v % base];
        v /= base;
    }
    while (i--) serial_putc(buf[i]);
}

void kprintf(const char *fmt, ...) {
    uint64_t flags = irq_save();
    spinlock_lock(&serial_lock);
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            serial_putc(*fmt);
            continue;
        }
        ++fmt;
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            serial_write(s ? s : "(null)");
        } else if (*fmt == 'u') {
            print_uint(va_arg(ap, unsigned), 10);
        } else if (*fmt == 'x') {
            print_uint(va_arg(ap, unsigned), 16);
        } else if (*fmt == 'l' && fmt[1] == 'x') {
            ++fmt;
            print_uint(va_arg(ap, uint64_t), 16);
        } else if (*fmt == 'p') {
            serial_write("0x");
            print_uint((uint64_t)(uintptr_t)va_arg(ap, void *), 16);
        } else {
            serial_putc(*fmt);
        }
    }
    va_end(ap);
    spinlock_unlock(&serial_lock);
    irq_restore(flags);
}
