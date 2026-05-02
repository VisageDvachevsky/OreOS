#include "kernel.h"

#define MAX_PROCESSES 32
#define USER_BASE 0x0000008000000000ULL
#define USER_LIMIT 0x0000800000000000ULL

static Process processes[MAX_PROCESSES];
static char process_names[MAX_PROCESSES][64];
static uint32_t process_count;
static Process *kernel_proc;
static Process *current_proc;

static void mem_zero_local(void *ptr, uint64_t size) {
    uint8_t *p = ptr;
    for (uint64_t i = 0; i < size; ++i) p[i] = 0;
}

static void mem_copy_local(void *dst, const void *src, uint64_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < size; ++i) d[i] = s[i];
}

static const char *store_process_name(uint32_t pid, const char *name) {
    if (pid >= MAX_PROCESSES) return "(invalid)";
    const char *src = name ? name : "(unnamed)";
    uint32_t i = 0;
    for (; i + 1 < sizeof(process_names[pid]) && src[i]; ++i) {
        process_names[pid][i] = src[i];
    }
    process_names[pid][i] = 0;
    return process_names[pid];
}

void process_init(void) {
    process_count = 1;
    kernel_proc = &processes[0];
    kernel_proc->pid = 0;
    kernel_proc->name = store_process_name(0, "kernel");
    kernel_proc->state = ORE_PROC_RUNNING;
    kernel_proc->address_space.pml4_phys = vmm_kernel_pml4();
    kernel_proc->address_space.user_base = USER_BASE;
    kernel_proc->address_space.user_limit = USER_LIMIT;
    current_proc = kernel_proc;
    kprintf("process: kernel process ready pml4 0x%lx\n", kernel_proc->address_space.pml4_phys);
}

Process *process_kernel(void) {
    return kernel_proc;
}

Process *process_create_kernel(const char *name) {
    if (process_count >= MAX_PROCESSES) return 0;
    Process *proc = &processes[process_count];
    mem_zero_local(proc, sizeof(*proc));
    proc->pid = process_count;
    proc->name = store_process_name(proc->pid, name);
    proc->state = ORE_PROC_READY;
    if (address_space_clone_kernel(&proc->address_space, &kernel_proc->address_space) < 0) return 0;
    process_count++;
    kprintf("process: created skeleton pid %u %s\n", proc->pid, name ? name : "(unnamed)");
    return proc;
}

Process *process_create_user(const char *name) {
    Process *proc = process_create_kernel(name);
    if (!proc) return 0;
    proc->address_space.pml4_phys = vmm_create_user_pml4();
    proc->user_alloc_next = 0x000000a000000000ULL;
    kprintf("process: user pid %u %s\n", proc->pid, name ? name : "(unnamed)");
    return proc;
}

Process *process_current(void) {
    return current_proc ? current_proc : kernel_proc;
}

void process_set_current(Process *process) {
    if (process) {
        current_proc = process;
        process->state = ORE_PROC_RUNNING;
    }
}

Process *process_find(uint32_t pid) {
    for (uint32_t i = 0; i < process_count; ++i) {
        if (processes[i].pid == pid) return &processes[i];
    }
    return 0;
}

uint32_t process_table_count(void) {
    return process_count;
}

Process *process_table_at(uint32_t index) {
    if (index >= process_count) return 0;
    return &processes[index];
}

static Process *next_ready_process(uint32_t exclude_pid) {
    if (process_count <= 1) return 0;
    uint32_t start = exclude_pid + 1;
    if (start == 0 || start >= process_count) start = 1;
    for (uint32_t scanned = 1; scanned < process_count; ++scanned) {
        uint32_t i = start + scanned - 1;
        if (i >= process_count) i = 1 + (i - process_count);
        Process *proc = &processes[i];
        if (proc->pid != exclude_pid && proc->state == ORE_PROC_READY) return proc;
    }
    return 0;
}

int process_schedule_user_tick(InterruptFrame *frame) {
    if (!frame || (frame->cs & 3) != 3) return 0;
    Process *current = process_current();
    if (!current || current->state != ORE_PROC_RUNNING) return 0;
    if (current->pid == 1) return 0;
    Process *next = next_ready_process(current->pid);
    if (!next) return 0;

    mem_copy_local(&current->user_frame, frame, sizeof(*frame));
    current->has_user_frame = 1;
    current->saved_user_rip = frame->rip;
    current->saved_user_rsp = frame->rsp;
    current->state = ORE_PROC_READY;

    if (next->has_user_frame) {
        mem_copy_local(frame, &next->user_frame, sizeof(*frame));
    } else {
        frame->r15 = frame->r14 = frame->r13 = frame->r12 = 0;
        frame->r11 = frame->r10 = frame->r9 = frame->r8 = 0;
        frame->rdi = frame->rsi = frame->rbp = frame->rdx = 0;
        frame->rcx = frame->rbx = frame->rax = 0;
        frame->vector = 32;
        frame->error_code = 0;
        frame->rip = next->saved_user_rip ? next->saved_user_rip : next->user_entry;
        frame->cs = 0x23;
        frame->rflags = 0x202;
        frame->rsp = next->saved_user_rsp ? next->saved_user_rsp : next->user_stack;
        frame->ss = 0x1b;
        mem_copy_local(&next->user_frame, frame, sizeof(*frame));
        next->has_user_frame = 1;
    }

    process_set_current(next);
    write_cr3(next->address_space.pml4_phys);
    kprintf("process: timer switch %u -> %u\n", current->pid, next->pid);
    return 1;
}

int address_space_clone_kernel(AddressSpace *dst, const AddressSpace *src) {
    if (!dst || !src) return -1;
    dst->pml4_phys = src->pml4_phys;
    dst->user_base = src->user_base;
    dst->user_limit = src->user_limit;
    return 0;
}
