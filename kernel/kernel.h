#ifndef ORE_KERNEL_H
#define ORE_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#include "ore_abi.h"

#define ORE_MAX_CPUS 64

typedef struct {
    uint32_t logical_id;
    uint8_t apic_id;
    volatile uint32_t started;
    volatile uint32_t online;
    uint64_t stack_base;
    uint64_t stack_top;
} CpuInfo;

typedef struct {
    volatile uint32_t value;
} Spinlock;

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} InterruptFrame;

typedef enum {
    THREAD_RUNNABLE = 0,
    THREAD_RUNNING = 1,
    THREAD_BLOCKED = 2,
    THREAD_SLEEPING = 3,
    THREAD_DEAD = 4
} ThreadState;

typedef struct Process Process;
typedef struct AddressSpace AddressSpace;

typedef struct Thread {
    uint32_t id;
    ThreadState state;
    uint32_t cpu_id;
    uint64_t *saved_sp;
    uint64_t stack_base;
    uint64_t stack_top;
    uint64_t switches;
    void (*entry)(void *);
    void *arg;
    const char *name;
    uint64_t wake_tick;
    Process *process;
    struct Thread *next;
} Thread;

typedef struct WaitQueue {
    Thread *head;
} WaitQueue;

typedef struct Mutex {
    Spinlock lock;
    uint32_t held;
    Thread *owner;
    WaitQueue waiters;
} Mutex;

struct AddressSpace {
    uint64_t pml4_phys;
    uint64_t user_base;
    uint64_t user_limit;
};

#define ORE_MAX_HANDLES 64
#define ORE_MAX_USER_ALLOCS 64

typedef struct UserAllocation {
    uint64_t virt;
    uint64_t phys;
    uint32_t pages;
    uint32_t used;
} UserAllocation;

typedef struct Handle {
    uint32_t type;
    uint64_t offset;
    uint32_t flags;
    char path[128];
} Handle;

struct Process {
    uint32_t pid;
    const char *name;
    uint32_t state;
    uint64_t user_entry;
    uint64_t user_stack;
    uint32_t user_segment_count;
    uint32_t parent_pid;
    uint64_t syscall_count;
    uint64_t saved_user_rip;
    uint64_t saved_user_rsp;
    uint32_t waiting_for_pid;
    uint64_t wait_return_rip;
    uint64_t wait_return_rsp;
    uint32_t has_user_frame;
    InterruptFrame user_frame;
    char args[128];
    AddressSpace address_space;
    Handle handles[ORE_MAX_HANDLES];
    UserAllocation user_allocs[ORE_MAX_USER_ALLOCS];
    uint64_t user_alloc_next;
    uint64_t user_alloc_pages_total;
    uint32_t exited;
    int32_t exit_code;
};

typedef OreVfsStat VfsStat;

typedef struct {
    uint64_t vaddr;
    uint64_t file_offset;
    uint64_t mem_size;
    uint64_t file_size;
    uint64_t flags;
} UserElfSegmentInfo;

typedef struct {
    uint64_t entry;
    uint64_t image_size;
    uint32_t segment_count;
    UserElfSegmentInfo segments[8];
} UserElfInfo;

extern CpuInfo g_cpus[ORE_MAX_CPUS];
extern uint32_t g_cpu_count;

void spinlock_lock(Spinlock *lock);
void spinlock_unlock(Spinlock *lock);

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
int serial_read_byte(void);
void kprintf(const char *fmt, ...);

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);
void idt_init(void);
void idt_load_current_cpu(void);
uint64_t *isr_dispatch(uint64_t vector, uint64_t error_code, InterruptFrame *frame);
void panic(const char *message) __attribute__((noreturn));

void acpi_init(uint64_t rsdp);
uint32_t acpi_cpu_count(void);
uint8_t acpi_cpu_apic_id(uint32_t index);
uint64_t acpi_lapic_base(void);

void pmm_init(const BootInfo *boot_info);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);
void *pmm_alloc_pages(uint32_t count);
void pmm_free_pages(void *ptr, uint32_t count);
void *early_alloc_pages(uint32_t pages);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages_count(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_reserved_pages(void);

void heap_init(void);
void *kmalloc(uint64_t size);
void *kzalloc(uint64_t size);
void kfree(void *ptr);
void memory_self_test(void);

#define VMM_PRESENT 0x001ULL
#define VMM_WRITE   0x002ULL
#define VMM_USER    0x004ULL
#define VMM_NX      (1ULL << 63)

void vmm_init(const BootInfo *boot_info);
int vmm_map(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
int vmm_map_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
int vmm_protect(uint64_t virt, uint64_t size, uint64_t flags);
int vmm_protect_in(uint64_t pml4_phys, uint64_t virt, uint64_t size, uint64_t flags);
void vmm_unmap(uint64_t virt, uint64_t size);
void vmm_unmap_in(uint64_t pml4_phys, uint64_t virt, uint64_t size);
uint64_t vmm_virt_to_phys(uint64_t virt);
uint64_t vmm_virt_to_phys_in(uint64_t pml4_phys, uint64_t virt);
uint64_t vmm_flags(uint64_t virt);
uint64_t vmm_kernel_pml4(void);
uint64_t vmm_create_user_pml4(void);
void vmm_self_test(const BootInfo *boot_info);

void process_init(void);
Process *process_kernel(void);
Process *process_create_kernel(const char *name);
Process *process_create_user(const char *name);
Process *process_current(void);
void process_set_current(Process *process);
Process *process_find(uint32_t pid);
uint32_t process_table_count(void);
Process *process_table_at(uint32_t index);
int process_schedule_user_tick(InterruptFrame *frame);
int address_space_clone_kernel(AddressSpace *dst, const AddressSpace *src);

void smp_init(void);
void ap_main(uint32_t cpu_id);
uint32_t current_cpu_id(void);
uint64_t read_cr3(void);
uint64_t read_cr2(void);
void write_cr3(uint64_t value);
uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);
void cpu_enable_nxe(void);

void pic_disable(void);
void lapic_init_current_cpu(void);
void lapic_eoi(void);
void lapic_timer_init_current_cpu(void);
uint64_t timer_ticks(uint32_t cpu_id);

void scheduler_init(void);
int thread_create(void (*fn)(void *), void *arg, uint32_t cpu_id, const char *name);
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_block(WaitQueue *queue);
void thread_unblock(Thread *thread);
void wait_queue_init(WaitQueue *queue);
void mutex_init(Mutex *mutex);
void mutex_lock(Mutex *mutex);
void mutex_unlock(Mutex *mutex);
void scheduler_self_test_start(void);
void scheduler_start_current_cpu(void);
uint64_t *scheduler_tick(uint32_t cpu_id, uint64_t *interrupted_sp);
uint64_t scheduler_switch_count(void);
void context_switch(uint64_t **old_sp_slot, uint64_t *new_sp);

void initramfs_init(const BootInfo *boot_info);
void vfs_init(void);
int vfs_mount_initramfs(void);
int vfs_stat(const char *path, VfsStat *stat);
int64_t vfs_read_file(const char *path, void *buffer, uint64_t buffer_size);
int64_t vfs_read_file_at(const char *path, uint64_t offset, void *buffer, uint64_t buffer_size);
int vfs_readdir(uint32_t index, VfsStat *stat);
int vfs_readdir_path(const char *path, uint32_t index, VfsStat *stat);
void vfs_self_test(void);
void okmod_init(void);
uint32_t okmod_count(void);
int okmod_info(uint32_t index, OreModuleInfo *info);
int okmod_run_command(const char *name, char *out, uint64_t out_len);

void console_init(const BootInfo *boot_info);
void console_write(const char *s);
void console_putc(char c);
int console_read_key(void);
int console_gfx_info(OreGfxInfo *info);
int console_gfx_present_indexed16(const uint8_t *buffer, uint32_t width, uint32_t height);
void input_state_poll(OreInputState *state);
void input_init(void);
int terrain_render_user(const OreTerrainJob *job, uint8_t *buffer, uint64_t len, OreTerrainResult *result);
void terrain_worker_poll(uint32_t cpu_id);
void net_init(void);
int net_info(OreNetInfo *info);
int64_t net_send_frame(const void *frame, uint64_t len);
int64_t net_recv_frame(void *buffer, uint64_t cap);

void elf_self_test(void);
int elf_probe_user_image(const void *image, uint64_t size, UserElfInfo *info);
void syscall_init(void);
uint64_t syscall_dispatch(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
uint64_t syscall_count_current(void);
uint64_t syscall_user_rip_current(void);
uint64_t syscall_user_rsp_current(void);
void syscall_override_return(uint64_t rip, uint64_t rsp);
int copy_from_user(void *dst, const void *src, uint64_t len);
int copy_to_user(void *dst, const void *src, uint64_t len);
int strlen_user(const char *s, uint64_t max, uint64_t *out_len);
void block_init(void);
int block_root_read(uint64_t lba, void *buffer, uint32_t blocks);
uint64_t block_root_count(void);
void user_init_spawn(const char *path) __attribute__((noreturn));
void user_enter(uint64_t entry, uint64_t stack) __attribute__((noreturn));

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void mmio_write32(uint64_t addr, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static inline uint32_t mmio_read32(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}

#endif
