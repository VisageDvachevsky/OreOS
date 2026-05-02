#include "kernel.h"

#define LAPIC_ID 0x20
#define LAPIC_EOI 0xb0
#define LAPIC_SVR 0xf0
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310
#define TRAMPOLINE_BASE 0x8000ULL
#define TRAMPOLINE_CFG_CR3 0x8f00ULL
#define TRAMPOLINE_CFG_STACK 0x8f08ULL
#define TRAMPOLINE_CFG_ENTRY 0x8f10ULL
#define TRAMPOLINE_CFG_CPU_ID 0x8f18ULL
#define TRAMPOLINE_CFG_STARTED 0x8f1cULL
#define TRAMPOLINE_GDT 0x8f40ULL
#define TRAMPOLINE_GDT_PTR 0x8f70ULL

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];

static void delay(void) {
    for (volatile uint32_t i = 0; i < 100000; ++i) __asm__ volatile("pause");
}

static uint32_t lapic_read(uint32_t reg) {
    return mmio_read32(acpi_lapic_base() + reg);
}

static void lapic_write(uint32_t reg, uint32_t value) {
    mmio_write32(acpi_lapic_base() + reg, value);
}

static void lapic_wait_icr(void) {
    while (lapic_read(LAPIC_ICR_LOW) & (1u << 12)) {}
}

static void lapic_send_ipi(uint8_t apic_id, uint32_t low) {
    lapic_wait_icr();
    lapic_write(LAPIC_ICR_HIGH, ((uint32_t)apic_id) << 24);
    lapic_write(LAPIC_ICR_LOW, low);
    lapic_wait_icr();
}

static void copy_memory(void *dst, const void *src, uint64_t len) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < len; ++i) d[i] = s[i];
}

static void write64(uint64_t addr, uint64_t value) {
    *(volatile uint64_t *)(uintptr_t)addr = value;
}

static void write32(uint64_t addr, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static void prepare_trampoline(void) {
    uint64_t size = (uint64_t)(ap_trampoline_end - ap_trampoline_start);
    if (size > 0xf00) panic("AP trampoline too large");
    copy_memory((void *)(uintptr_t)TRAMPOLINE_BASE, ap_trampoline_start, size);

    volatile uint64_t *gdt = (volatile uint64_t *)(uintptr_t)TRAMPOLINE_GDT;
    gdt[0] = 0;
    gdt[1] = 0x00cf9a000000ffffULL;
    gdt[2] = 0x00cf92000000ffffULL;
    gdt[3] = 0x00af9a000000ffffULL;
    gdt[4] = 0x00af92000000ffffULL;

    volatile uint16_t *gdt_limit = (volatile uint16_t *)(uintptr_t)TRAMPOLINE_GDT_PTR;
    volatile uint32_t *gdt_base = (volatile uint32_t *)(uintptr_t)(TRAMPOLINE_GDT_PTR + 2);
    *gdt_limit = (uint16_t)(5 * 8 - 1);
    *gdt_base = (uint32_t)TRAMPOLINE_GDT;
}

static int wait_for_cpu(CpuInfo *cpu) {
    for (uint32_t i = 0; i < 50000000; ++i) {
        if (cpu->online) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

void smp_init(void) {
    uint32_t cpus = acpi_cpu_count();
    if (cpus == 0) {
        kprintf("SMP: no MADT CPU entries, skipping AP startup\n");
        return;
    }
    lapic_init_current_cpu();
    uint8_t bsp_apic = (uint8_t)(lapic_read(LAPIC_ID) >> 24);
    kprintf("SMP: BSP APIC ID %u\n", bsp_apic);
    g_cpu_count = 0;
    for (uint32_t i = 0; i < cpus && g_cpu_count < ORE_MAX_CPUS; ++i) {
        g_cpus[g_cpu_count].logical_id = g_cpu_count;
        g_cpus[g_cpu_count].apic_id = acpi_cpu_apic_id(i);
        g_cpus[g_cpu_count].started = g_cpus[g_cpu_count].apic_id == bsp_apic;
        g_cpus[g_cpu_count].online = g_cpus[g_cpu_count].apic_id == bsp_apic;
        g_cpu_count++;
    }
    prepare_trampoline();
    for (uint32_t i = 0; i < cpus; ++i) {
        uint8_t apic_id = acpi_cpu_apic_id(i);
        if (apic_id == bsp_apic) continue;
        CpuInfo *cpu = 0;
        for (uint32_t j = 0; j < g_cpu_count; ++j) {
            if (g_cpus[j].apic_id == apic_id) cpu = &g_cpus[j];
        }
        if (!cpu) continue;
        void *stack = pmm_alloc_pages(4);
        if (!stack) panic("no AP stack pages");
        cpu->stack_base = (uint64_t)(uintptr_t)stack;
        cpu->stack_top = cpu->stack_base + 4 * 4096;

        write64(TRAMPOLINE_CFG_CR3, read_cr3());
        write64(TRAMPOLINE_CFG_STACK, cpu->stack_top);
        write64(TRAMPOLINE_CFG_ENTRY, (uint64_t)(uintptr_t)ap_main);
        write32(TRAMPOLINE_CFG_CPU_ID, cpu->logical_id);
        write32(TRAMPOLINE_CFG_STARTED, 0);

        kprintf("SMP: starting CPU %u APIC ID %u\n", cpu->logical_id, apic_id);
        lapic_send_ipi(apic_id, 0x00004500);
        delay();
        lapic_send_ipi(apic_id, 0x00000600 | (TRAMPOLINE_BASE >> 12));
        delay();
        lapic_send_ipi(apic_id, 0x00000600 | (TRAMPOLINE_BASE >> 12));
        delay();
        if (!wait_for_cpu(cpu)) {
            kprintf("SMP: CPU %u did not come online\n", cpu->logical_id);
        }
    }
    lapic_write(LAPIC_EOI, 0);
    kprintf("SMP: all APs online\n");
}

void ap_main(uint32_t cpu_id) {
    gdt_init();
    idt_load_current_cpu();
    lapic_init_current_cpu();
    if (cpu_id < g_cpu_count) {
        g_cpus[cpu_id].started = 1;
        g_cpus[cpu_id].online = 1;
    }
    kprintf("SMP: CPU %u entered ap_main\n", cpu_id);
    lapic_timer_init_current_cpu();
    for (;;) {
        terrain_worker_poll(cpu_id);
        __asm__ volatile("hlt");
    }
}
