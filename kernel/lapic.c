#include "kernel.h"

#define LAPIC_EOI 0x0b0
#define LAPIC_SVR 0x0f0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INITCNT 0x380
#define LAPIC_TIMER_CURCNT 0x390
#define LAPIC_TIMER_DIVIDE 0x3e0

#define TIMER_VECTOR 32
#define TIMER_PERIODIC (1u << 17)
#define TIMER_MASKED (1u << 16)

volatile uint64_t g_timer_ticks[ORE_MAX_CPUS];

static uint32_t lapic_read_reg(uint32_t reg) {
    return mmio_read32(acpi_lapic_base() + reg);
}

static void lapic_write_reg(uint32_t reg, uint32_t value) {
    mmio_write32(acpi_lapic_base() + reg, value);
}

void pic_disable(void) {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}

void lapic_init_current_cpu(void) {
    lapic_write_reg(LAPIC_SVR, lapic_read_reg(LAPIC_SVR) | 0x100 | 0xff);
    lapic_write_reg(LAPIC_EOI, 0);
}

void lapic_eoi(void) {
    lapic_write_reg(LAPIC_EOI, 0);
}

void lapic_timer_init_current_cpu(void) {
    uint32_t cpu = current_cpu_id();
    if (cpu < ORE_MAX_CPUS) {
        __atomic_store_n(&g_timer_ticks[cpu], 0, __ATOMIC_RELAXED);
    }

    lapic_write_reg(LAPIC_TIMER_DIVIDE, 0x3);
    lapic_write_reg(LAPIC_LVT_TIMER, TIMER_VECTOR | TIMER_PERIODIC);
    lapic_write_reg(LAPIC_TIMER_INITCNT, 10000000);
    __asm__ volatile("sti");
    kprintf("timer: CPU %u LAPIC timer online\n", cpu);
}

uint64_t timer_ticks(uint32_t cpu_id) {
    if (cpu_id >= ORE_MAX_CPUS) return 0;
    return __atomic_load_n(&g_timer_ticks[cpu_id], __ATOMIC_RELAXED);
}
