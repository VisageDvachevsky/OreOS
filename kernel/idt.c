#include "kernel.h"

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) IdtPtr;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) IdtEntry;

static IdtEntry idt[256];

extern void *isr_stub_table[];

static void set_gate(uint8_t vector, void *handler) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)addr;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8e;
    idt[vector].offset_mid = (uint16_t)(addr >> 16);
    idt[vector].offset_high = (uint32_t)(addr >> 32);
    idt[vector].zero = 0;
}

void idt_init(void) {
    for (uint16_t i = 0; i <= 32; ++i) set_gate((uint8_t)i, isr_stub_table[i]);
    idt_load_current_cpu();
}

void idt_load_current_cpu(void) {
    IdtPtr ptr = { .limit = sizeof(idt) - 1, .base = (uint64_t)(uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(ptr) : "memory");
}

uint64_t *isr_dispatch(uint64_t vector, uint64_t error_code, InterruptFrame *frame) {
    if (vector == 32) {
        uint32_t cpu = current_cpu_id();
        extern volatile uint64_t g_timer_ticks[ORE_MAX_CPUS];
        if (cpu < ORE_MAX_CPUS) {
            __atomic_add_fetch(&g_timer_ticks[cpu], 1, __ATOMIC_RELAXED);
        }
        lapic_eoi();
        if (cpu == 0 && process_schedule_user_tick(frame)) return 0;
        return scheduler_tick(cpu, (uint64_t *)frame);
    }

    if (vector == 14) {
        if (frame && (frame->cs & 3) == 3) {
            Process *proc = process_current();
            kprintf("user fault: pid %u cr2 0x%lx error 0x%lx rip 0x%lx syscalls 0x%lx\n",
                    proc ? proc->pid : 0, read_cr2(), error_code, frame->rip, syscall_count_current());
            panic("user process fault");
        }
        kprintf("page fault: cr2 0x%lx error 0x%lx rip 0x%lx\n",
                read_cr2(), error_code, frame ? frame->rip : 0);
    }
    kprintf("ISR: fatal vector %u error 0x%lx rip 0x%lx cs 0x%lx rflags 0x%lx\n",
            (unsigned)vector,
            error_code,
            frame ? frame->rip : 0,
            frame ? frame->cs : 0,
            frame ? frame->rflags : 0);
    panic("unhandled CPU exception");
}
