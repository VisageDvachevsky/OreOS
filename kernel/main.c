#include "kernel.h"

static void demo_thread(void *arg) {
    uint64_t id = (uint64_t)(uintptr_t)arg;
    uint64_t tick = 0;
    for (;;) {
        tick++;
        if (tick == 1 || (tick & 0xfffff) == 0) {
            kprintf("thread %u tick 0x%lx\n", (unsigned)id, tick);
            if (id == 1) {
                kprintf("scheduler: switches 0x%lx\n", scheduler_switch_count());
            }
        }
        thread_yield();
    }
}

void kernel_entry(const BootInfo *boot_info) {
    serial_init();
    kprintf("\nOre kernel: x86_64 entry\n");
    if (!boot_info || boot_info->magic != ORE_BOOTINFO_MAGIC) {
        kprintf("BootInfo: invalid\n");
        for (;;) __asm__ volatile("hlt");
    }
    gdt_init();
    idt_init();
    pic_disable();
#if ORE_TEST_EXCEPTION
    kprintf("test: triggering #UD exception\n");
    __asm__ volatile("ud2");
#endif
    kprintf("BootInfo: kernel base 0x%lx size 0x%lx\n", boot_info->kernel_base, boot_info->kernel_size);
    kprintf("BootInfo: mem entries %u framebuffer 0x%lx %ux%u\n",
            (unsigned)boot_info->memory_map_entries,
            boot_info->framebuffer.base,
            boot_info->framebuffer.width,
            boot_info->framebuffer.height);
    pmm_init(boot_info);
    heap_init();
    memory_self_test();
    acpi_init(boot_info->rsdp);
    cpu_enable_nxe();
    vmm_init(boot_info);
    vmm_self_test(boot_info);
    console_init(boot_info);
    input_init();
    net_init();
    initramfs_init(boot_info);
    block_init();
    vfs_init();
    vfs_mount_initramfs();
    vfs_self_test();
    okmod_init();
    syscall_init();
    elf_self_test();
    process_init();
    scheduler_init();
    smp_init();
    (void)demo_thread;
    scheduler_start_current_cpu();
    lapic_timer_init_current_cpu();
    for (volatile uint64_t i = 0; i < 500000000ULL; ++i) {
        if (timer_ticks(0) > 4) break;
        __asm__ volatile("hlt");
    }
    user_init_spawn("/init");
}
