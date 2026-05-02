#include "kernel.h"

#define MSR_EFER 0xc0000080U
#define MSR_STAR 0xc0000081U
#define MSR_LSTAR 0xc0000082U
#define MSR_FMASK 0xc0000084U
#define EFER_SCE 1ULL

#define USER_BASE 0x0000008000000000ULL
#define USER_LIMIT 0x0000800000000000ULL
#define USER_ALLOC_BASE 0x000000a000000000ULL
#define USER_ALLOC_LIMIT (USER_ALLOC_BASE + 4ULL * 1024ULL * 1024ULL)

extern void syscall_entry_asm(void);

static uint64_t syscall_count;
static uint32_t next_synthetic_pid = 2;
static uint32_t last_synthetic_pid;
volatile uint64_t g_syscall_user_rip;
volatile uint64_t g_syscall_user_rsp;
volatile uint64_t g_sysret_override;
volatile uint64_t g_sysret_rip;
volatile uint64_t g_sysret_rsp;

static int user_range_ok(uint64_t ptr, uint64_t len) {
    if (!ptr || ptr < USER_BASE || ptr >= USER_LIMIT) return 0;
    if (len > USER_LIMIT - ptr) return 0;
    for (uint64_t off = 0; off < len; off += 4096) {
        if (!vmm_virt_to_phys(ptr + off)) return 0;
    }
    if (len && !vmm_virt_to_phys(ptr + len - 1)) return 0;
    return 1;
}

int copy_from_user(void *dst, const void *src, uint64_t len) {
    if (!len) return 0;
    if (!user_range_ok((uint64_t)(uintptr_t)src, len)) return -ORE_EFAULT;
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < len; ++i) d[i] = s[i];
    return 0;
}

int copy_to_user(void *dst, const void *src, uint64_t len) {
    if (!len) return 0;
    if (!user_range_ok((uint64_t)(uintptr_t)dst, len)) return -ORE_EFAULT;
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < len; ++i) d[i] = s[i];
    return 0;
}

int strlen_user(const char *s, uint64_t max, uint64_t *out_len) {
    for (uint64_t i = 0; i < max; ++i) {
        if (!user_range_ok((uint64_t)(uintptr_t)s + i, 1)) return -ORE_EFAULT;
        if (s[i] == 0) {
            if (out_len) *out_len = i;
            return 0;
        }
    }
    return -ORE_EINVAL;
}

static void copy_string(char *dst, const char *src, uint64_t max) {
    uint64_t i = 0;
    for (; i + 1 < max && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static int copy_user_string_optional(uint64_t user_ptr, char *dst, uint64_t max) {
    if (!dst || !max) return -ORE_EINVAL;
    dst[0] = 0;
    if (!user_ptr) return 0;
    const char *src = (const char *)(uintptr_t)user_ptr;
    uint64_t len = 0;
    int err = strlen_user(src, max - 1, &len);
    if (err < 0) return err;
    err = copy_from_user(dst, src, len + 1);
    if (err < 0) return err;
    return 0;
}

static int64_t user_alloc_pages_sys(uint64_t count) {
    Process *proc = process_current();
    if (!proc || proc == process_kernel()) return -ORE_EINVAL;
    if (!count || count > 1024) return -ORE_EINVAL;
    if (proc->user_alloc_pages_total + count > 1024) return -ORE_ENOMEM;
    uint32_t slot = ORE_MAX_USER_ALLOCS;
    for (uint32_t i = 0; i < ORE_MAX_USER_ALLOCS; ++i) {
        if (!proc->user_allocs[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == ORE_MAX_USER_ALLOCS) return -ORE_ENOMEM;
    uint64_t bytes = count * 4096ULL;
    uint64_t va = (proc->user_alloc_next + 4095ULL) & ~4095ULL;
    if (va < USER_ALLOC_BASE) va = USER_ALLOC_BASE;
    if (va + bytes < va || va + bytes > USER_ALLOC_LIMIT) return -ORE_ENOMEM;
    void *phys = pmm_alloc_pages((uint32_t)count);
    if (!phys) return -ORE_ENOMEM;
    uint8_t *p = phys;
    for (uint64_t i = 0; i < bytes; ++i) p[i] = 0;
    if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)phys, bytes, VMM_USER | VMM_WRITE | VMM_NX) < 0) {
        pmm_free_pages(phys, (uint32_t)count);
        return -ORE_EINVAL;
    }
    proc->user_allocs[slot].virt = va;
    proc->user_allocs[slot].phys = (uint64_t)(uintptr_t)phys;
    proc->user_allocs[slot].pages = (uint32_t)count;
    proc->user_allocs[slot].used = 1;
    proc->user_alloc_pages_total += count;
    proc->user_alloc_next = va + bytes;
    return (int64_t)va;
}

static int64_t user_free_pages_sys(uint64_t ptr, uint64_t count) {
    Process *proc = process_current();
    if (!proc || proc == process_kernel() || !ptr || !count) return -ORE_EINVAL;
    for (uint32_t i = 0; i < ORE_MAX_USER_ALLOCS; ++i) {
        UserAllocation *alloc = &proc->user_allocs[i];
        if (alloc->used && alloc->virt == ptr && alloc->pages == (uint32_t)count) {
            uint64_t bytes = (uint64_t)alloc->pages * 4096ULL;
            vmm_unmap_in(proc->address_space.pml4_phys, alloc->virt, bytes);
            pmm_free_pages((void *)(uintptr_t)alloc->phys, alloc->pages);
            proc->user_alloc_pages_total -= alloc->pages;
            alloc->used = 0;
            alloc->virt = 0;
            alloc->phys = 0;
            alloc->pages = 0;
            return 0;
        }
    }
    return -ORE_EINVAL;
}

static Handle *current_handles(void) {
    Process *proc = process_current();
    return proc ? proc->handles : process_kernel()->handles;
}

static uint64_t align_down_local(uint64_t value) {
    return value & ~4095ULL;
}

static uint64_t align_up_local(uint64_t value) {
    return (value + 4095ULL) & ~4095ULL;
}

static uint64_t flags_for_elf_segment(uint64_t ph_flags) {
    uint64_t flags = VMM_USER;
    if (ph_flags & 2) flags |= VMM_WRITE;
    return flags;
}

static int map_zeroed_user_pages(Process *proc, uint64_t start, uint64_t size, uint64_t flags) {
    uint64_t page_start = align_down_local(start);
    uint64_t page_end = align_up_local(start + size);
    for (uint64_t va = page_start; va < page_end; va += 4096ULL) {
        if (!vmm_virt_to_phys_in(proc->address_space.pml4_phys, va)) {
            void *page = pmm_alloc_page();
            if (!page) return -ORE_ENOMEM;
            uint8_t *p = page;
            for (uint64_t i = 0; i < 4096; ++i) p[i] = 0;
            if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)page, 4096ULL, flags | VMM_WRITE) < 0) {
                return -ORE_EINVAL;
            }
        }
    }
    return 0;
}

static int copy_to_user_phys(Process *proc, uint64_t va, const uint8_t *src, uint64_t size) {
    for (uint64_t off = 0; off < size; ++off) {
        uint64_t phys = vmm_virt_to_phys_in(proc->address_space.pml4_phys, va + off);
        if (!phys) return -ORE_EINVAL;
        *(uint8_t *)(uintptr_t)phys = src[off];
    }
    return 0;
}

static int load_user_image_into_process(Process *proc, const uint8_t *image, const UserElfInfo *info) {
    if (!proc || !image || !info) return -1;
    for (uint32_t i = 0; i < info->segment_count; ++i) {
        const UserElfSegmentInfo *seg = &info->segments[i];
        uint64_t start = align_down_local(seg->vaddr);
        uint64_t end = align_up_local(seg->vaddr + seg->mem_size);
        for (uint64_t va = start; va < end; va += 4096ULL) {
            if (!vmm_virt_to_phys_in(proc->address_space.pml4_phys, va)) {
                void *page = pmm_alloc_page();
                if (!page) return -1;
                if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)page, 4096ULL, VMM_USER | VMM_WRITE) < 0) {
                    return -1;
                }
            }
        }
        for (uint64_t off = 0; off < seg->mem_size; ++off) {
            uint64_t phys = vmm_virt_to_phys_in(proc->address_space.pml4_phys, seg->vaddr + off);
            if (!phys) return -1;
            *(uint8_t *)(uintptr_t)phys = 0;
        }
        for (uint64_t off = 0; off < seg->file_size; ++off) {
            uint64_t phys = vmm_virt_to_phys_in(proc->address_space.pml4_phys, seg->vaddr + off);
            if (!phys) return -1;
            *(uint8_t *)(uintptr_t)phys = image[seg->file_offset + off];
        }
        if (vmm_protect_in(proc->address_space.pml4_phys, start, end - start, flags_for_elf_segment(seg->flags)) < 0) {
            return -1;
        }
    }

    uint64_t stack_top = 0x0000009000000000ULL;
    uint64_t stack_base = stack_top - 8ULL * 4096ULL;
    for (uint64_t va = stack_base; va < stack_top; va += 4096ULL) {
        void *page = pmm_alloc_page();
        if (!page) return -1;
        if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)page, 4096ULL, VMM_USER | VMM_WRITE | VMM_NX) < 0) {
            return -1;
        }
    }
    proc->user_entry = info->entry;
    proc->user_stack = stack_top - 16;
    proc->saved_user_rip = proc->user_entry;
    proc->saved_user_rsp = proc->user_stack;
    proc->user_segment_count = info->segment_count;
    return 0;
}

static int load_ore_image_into_process(Process *proc, const uint8_t *image, uint64_t image_len) {
    if (!proc || !image || image_len < sizeof(OreImageHeader)) return -ORE_EINVAL;
    const OreImageHeader *hdr = (const OreImageHeader *)image;
    if (hdr->magic != ORE_IMAGE_MAGIC || hdr->version != ORE_IMAGE_VERSION) return -ORE_EINVAL;
    if (hdr->header_size < sizeof(OreImageHeader) || hdr->header_size > 4096) return -ORE_EINVAL;
    if (!hdr->text_size || hdr->text_size > 256 * 1024ULL || hdr->data_size > 256 * 1024ULL || hdr->bss_size > 256 * 1024ULL) return -ORE_EINVAL;
    uint64_t reloc_bytes = hdr->reloc_count * sizeof(OreImageReloc);
    uint64_t text_off = hdr->header_size + reloc_bytes;
    uint64_t data_off = text_off + hdr->text_size;
    if (hdr->reloc_count > 256 || text_off < hdr->header_size || data_off < text_off || data_off + hdr->data_size > image_len) return -ORE_EINVAL;

    uint64_t text_base = USER_BASE;
    uint64_t data_base = align_up_local(text_base + hdr->text_size);
    uint64_t data_total = hdr->data_size + hdr->bss_size;
    if (hdr->entry >= hdr->text_size) return -ORE_EINVAL;
    int err = map_zeroed_user_pages(proc, text_base, hdr->text_size, VMM_USER);
    if (err < 0) return err;
    err = map_zeroed_user_pages(proc, data_base, data_total ? data_total : 1, VMM_USER | VMM_NX);
    if (err < 0) return err;
    err = copy_to_user_phys(proc, text_base, image + text_off, hdr->text_size);
    if (err < 0) return err;
    if (hdr->data_size) {
        err = copy_to_user_phys(proc, data_base, image + data_off, hdr->data_size);
        if (err < 0) return err;
    }

    const OreImageReloc *relocs = (const OreImageReloc *)(image + hdr->header_size);
    for (uint64_t i = 0; i < hdr->reloc_count; ++i) {
        const OreImageReloc *r = &relocs[i];
        uint64_t place = (r->segment == 0 ? text_base : data_base) + r->offset;
        uint64_t target = (r->segment == 0 ? text_base : data_base) + r->symbol_offset;
        uint64_t phys = vmm_virt_to_phys_in(proc->address_space.pml4_phys, place);
        if (!phys) return -ORE_EINVAL;
        if (r->type == ORE_IMAGE_RELOC_ABS64) {
            *(uint64_t *)(uintptr_t)phys = target;
        } else if (r->type == ORE_IMAGE_RELOC_PC32) {
            int64_t disp = (int64_t)target - (int64_t)(place + 4);
            if (disp < INT32_MIN || disp > INT32_MAX) return -ORE_EINVAL;
            *(int32_t *)(uintptr_t)phys = (int32_t)disp;
        } else {
            return -ORE_EINVAL;
        }
    }

    if (vmm_protect_in(proc->address_space.pml4_phys, text_base, align_up_local(hdr->text_size), VMM_USER) < 0) return -ORE_EINVAL;
    if (vmm_protect_in(proc->address_space.pml4_phys, data_base, align_up_local(data_total ? data_total : 1), VMM_USER | VMM_WRITE | VMM_NX) < 0) return -ORE_EINVAL;

    uint64_t stack_top = 0x0000009000000000ULL;
    uint64_t stack_base = stack_top - 8ULL * 4096ULL;
    for (uint64_t va = stack_base; va < stack_top; va += 4096ULL) {
        void *page = pmm_alloc_page();
        if (!page) return -ORE_ENOMEM;
        uint8_t *p = page;
        for (uint64_t z = 0; z < 4096; ++z) p[z] = 0;
        if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)page, 4096ULL, VMM_USER | VMM_WRITE | VMM_NX) < 0) {
            return -ORE_EINVAL;
        }
    }
    proc->user_entry = text_base + hdr->entry;
    proc->user_stack = stack_top - 16;
    proc->saved_user_rip = proc->user_entry;
    proc->saved_user_rsp = proc->user_stack;
    proc->user_segment_count = 2;
    return 0;
}

static Process *next_ready_process(uint32_t exclude_pid) {
    uint32_t count = process_table_count();
    if (count <= 1) return 0;
    uint32_t start = exclude_pid + 1;
    if (start == 0 || start >= count) start = 1;
    for (uint32_t scanned = 1; scanned < count; ++scanned) {
        uint32_t i = start + scanned - 1;
        if (i >= count) i = 1 + (i - count);
        Process *proc = process_table_at(i);
        if (proc && proc->pid != exclude_pid && proc->state == ORE_PROC_READY) return proc;
    }
    return 0;
}

static void enter_process_from_syscall(Process *next) {
    process_set_current(next);
    write_cr3(next->address_space.pml4_phys);
    syscall_override_return(next->saved_user_rip ? next->saved_user_rip : next->user_entry,
                            next->saved_user_rsp ? next->saved_user_rsp : next->user_stack);
}

static int schedule_next_from_syscall(void) {
    Process *current = process_current();
    Process *next = next_ready_process(current ? current->pid : 0);
    if (!next) return 0;
    if (current && current->state == ORE_PROC_RUNNING) {
        current->saved_user_rip = syscall_user_rip_current();
        current->saved_user_rsp = syscall_user_rsp_current();
        current->state = ORE_PROC_READY;
    }
    kprintf("process: yield switch %u -> %u\n", current ? current->pid : 0, next->pid);
    enter_process_from_syscall(next);
    return 1;
}

uint64_t syscall_count_current(void) {
    Process *proc = process_current();
    return proc ? proc->syscall_count : syscall_count;
}

uint64_t syscall_user_rip_current(void) {
    return g_syscall_user_rip;
}

uint64_t syscall_user_rsp_current(void) {
    return g_syscall_user_rsp;
}

void syscall_override_return(uint64_t rip, uint64_t rsp) {
    g_sysret_rip = rip;
    g_sysret_rsp = rsp;
    g_sysret_override = 1;
}

void syscall_init(void) {
    uint64_t efer = read_msr(MSR_EFER);
    write_msr(MSR_EFER, efer | EFER_SCE);
    write_msr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry_asm);
    write_msr(MSR_FMASK, 0x200ULL);
    write_msr(MSR_STAR, ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48));
    kprintf("syscall: x86_64 MSRs prepared\n");
}

uint64_t syscall_dispatch(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4;
    (void)a5;
    syscall_count++;
    Process *current_for_count = process_current();
    if (current_for_count) current_for_count->syscall_count++;
    if (number == ORE_SYS_WRITE) {
        if ((a1 != 1 && a1 != 2) || !user_range_ok(a2, a3)) return (uint64_t)-ORE_EFAULT;
        const char *s = (const char *)(uintptr_t)a2;
        for (uint64_t i = 0; i < a3; ++i) {
            serial_putc(s[i]);
            console_putc(s[i]);
        }
        return a3;
    }
    if (number == ORE_SYS_READ) {
        if (a1 != 0) return (uint64_t)-ORE_EBADF;
        if (!user_range_ok(a2, a3)) return (uint64_t)-ORE_EFAULT;
        if (a3 == 0) return 0;
        int key = -1;
        while (key < 0) {
            key = console_read_key();
            if (key < 0) __asm__ volatile("pause");
        }
        ((char *)(uintptr_t)a2)[0] = (char)key;
        return 1;
    }
    if (number == ORE_SYS_EXIT) {
        Process *proc = process_current();
        if (proc) {
            proc->exited = 1;
            proc->exit_code = (int32_t)a1;
            proc->state = ORE_PROC_EXITED;
            proc->saved_user_rip = 0;
            proc->saved_user_rsp = 0;
        }
        kprintf("user: process %u exited code %u syscalls 0x%lx\n",
                proc ? proc->pid : 0,
                (unsigned)a1,
                proc ? proc->syscall_count : syscall_count);
        if (proc && proc->parent_pid) {
            Process *parent = process_find(proc->parent_pid);
            if (parent && parent->wait_return_rip && parent->wait_return_rsp) {
                process_set_current(parent);
                write_cr3(parent->address_space.pml4_phys);
                syscall_override_return(parent->wait_return_rip, parent->wait_return_rsp);
                parent->wait_return_rip = 0;
                parent->wait_return_rsp = 0;
                parent->waiting_for_pid = 0;
                kprintf("process: returned to parent pid %u from child %u\n", parent->pid, proc->pid);
                return (uint64_t)(uint32_t)proc->exit_code;
            }
            if (parent && parent->state == ORE_PROC_BLOCKED && parent->waiting_for_pid == proc->pid) {
                parent->waiting_for_pid = 0;
                parent->state = ORE_PROC_READY;
            }
        }
        Process *next = next_ready_process(proc ? proc->pid : 0);
        if (next) {
            enter_process_from_syscall(next);
            return 0;
        }
        for (;;) __asm__ volatile("hlt");
    }
    if (number == ORE_SYS_YIELD) {
        schedule_next_from_syscall();
        return 0;
    }
    if (number == ORE_SYS_OPEN) {
        const char *path = (const char *)(uintptr_t)a1;
        uint64_t len = 0;
        int err = strlen_user(path, 127, &len);
        if (err < 0) return (uint64_t)err;
        char path_copy[128];
        err = copy_from_user(path_copy, path, len + 1);
        if (err < 0) return (uint64_t)err;
        VfsStat st;
        if (vfs_stat(path_copy, &st) < 0) return (uint64_t)-ORE_ENOENT;
        Handle *handles = current_handles();
        for (uint32_t i = 1; i < ORE_MAX_HANDLES; ++i) {
            if (!handles[i].type) {
                handles[i].type = st.type;
                handles[i].offset = 0;
                copy_string(handles[i].path, path_copy, sizeof(handles[i].path));
                return i;
            }
        }
        return (uint64_t)-ORE_EINVAL;
    }
    if (number == ORE_SYS_READDIR) {
        VfsStat st;
        if (a3) {
            const char *path = (const char *)(uintptr_t)a3;
            uint64_t len = 0;
            int err = strlen_user(path, 127, &len);
            if (err < 0) return (uint64_t)err;
            char path_copy[128];
            err = copy_from_user(path_copy, path, len + 1);
            if (err < 0) return (uint64_t)err;
            if (vfs_readdir_path(path_copy, (uint32_t)a1, &st) < 0) return (uint64_t)-ORE_ENOENT;
        } else if (vfs_readdir((uint32_t)a1, &st) < 0) {
            return (uint64_t)-ORE_ENOENT;
        }
        int err = copy_to_user((void *)(uintptr_t)a2, &st, sizeof(st));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_STAT) {
        const char *path = (const char *)(uintptr_t)a1;
        uint64_t len = 0;
        int err = strlen_user(path, 127, &len);
        if (err < 0) return (uint64_t)err;
        char path_copy[128];
        err = copy_from_user(path_copy, path, len + 1);
        if (err < 0) return (uint64_t)err;
        VfsStat st;
        if (vfs_stat(path_copy, &st) < 0) return (uint64_t)-ORE_ENOENT;
        err = copy_to_user((void *)(uintptr_t)a2, &st, sizeof(st));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_CLOSE) {
        Handle *handles = current_handles();
        if (a1 >= ORE_MAX_HANDLES || !handles[a1].type) return (uint64_t)-ORE_EBADF;
        handles[a1].type = 0;
        handles[a1].offset = 0;
        handles[a1].path[0] = 0;
        return 0;
    }
    if (number == ORE_SYS_FILE_READ) {
        Handle *handles = current_handles();
        if (a1 >= ORE_MAX_HANDLES || handles[a1].type != ORE_VFS_FILE) return (uint64_t)-ORE_EBADF;
        if (!user_range_ok(a2, a3)) return (uint64_t)-ORE_EFAULT;
        int64_t n = vfs_read_file_at(handles[a1].path, handles[a1].offset, (void *)(uintptr_t)a2, a3);
        if (n > 0) handles[a1].offset += (uint64_t)n;
        return (uint64_t)n;
    }
    if (number == ORE_SYS_INFO) {
        OreSysInfo info;
        info.total_pages = pmm_total_pages();
        info.free_pages = pmm_free_pages_count();
        info.used_pages = pmm_used_pages();
        info.reserved_pages = pmm_reserved_pages();
        info.uptime_ticks = timer_ticks(0);
        info.cpu_count = g_cpu_count;
        Process *proc = process_current();
        info.pid = proc ? proc->pid : 0;
        int err = copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_SPAWN) {
        const char *path = (const char *)(uintptr_t)a1;
        uint64_t len = 0;
        int err = strlen_user(path, 127, &len);
        if (err < 0) return (uint64_t)err;
        char path_copy[128];
        err = copy_from_user(path_copy, path, len + 1);
        if (err < 0) return (uint64_t)err;
        char args_copy[128];
        err = copy_user_string_optional(a2, args_copy, sizeof(args_copy));
        if (err < 0) return (uint64_t)err;
        VfsStat st;
        if (vfs_stat(path_copy, &st) < 0) return (uint64_t)-ORE_ENOENT;
        if (st.type != ORE_VFS_FILE || st.size == 0 || st.size > 512 * 1024ULL) return (uint64_t)-ORE_EINVAL;
        uint8_t *image = kmalloc(st.size);
        if (!image) return (uint64_t)-ORE_EINVAL;
        int64_t n = vfs_read_file(path_copy, image, st.size);
        if (n != (int64_t)st.size) {
            kfree(image);
            return (uint64_t)-ORE_EINVAL;
        }
        Process *child = process_create_user(path_copy);
        if (!child) {
            kfree(image);
            return (uint64_t)-ORE_EINVAL;
        }
        UserElfInfo exec_info;
        uint32_t spawned_ore_image = 0;
        if (st.size >= sizeof(OreImageHeader) &&
            ((const OreImageHeader *)image)->magic == ORE_IMAGE_MAGIC) {
            err = load_ore_image_into_process(child, image, st.size);
            if (err < 0) {
                kprintf("process: OreImage load failed pid %u path %s err %d size 0x%lx\n",
                        child->pid, path_copy, err, st.size);
                kfree(image);
                return (uint64_t)err;
            }
            spawned_ore_image = 1;
        } else {
            if (elf_probe_user_image(image, st.size, &exec_info) < 0) {
                kfree(image);
                return (uint64_t)-ORE_EINVAL;
            }
            if (load_user_image_into_process(child, image, &exec_info) < 0) {
                kfree(image);
                return (uint64_t)-ORE_EINVAL;
            }
        }
        kfree(image);
        child->state = ORE_PROC_READY;
        child->exited = 0;
        child->exit_code = 0;
        child->parent_pid = process_current() ? process_current()->pid : 0;
        copy_string(child->args, args_copy, sizeof(child->args));
        last_synthetic_pid = child->pid ? child->pid : next_synthetic_pid++;
        kprintf("process: spawn prepared pid %u path %s entry 0x%lx segments %u%s\n",
                last_synthetic_pid, path_copy, child->user_entry,
                child->user_segment_count, spawned_ore_image ? " ore-image" : "");
        return last_synthetic_pid;
    }
    if (number == ORE_SYS_WAIT) {
        Process *current = process_current();
        Process *child = process_find((uint32_t)a1);
        if (!child || !current || child->pid == current->pid) return (uint64_t)-ORE_EINVAL;
        if (child->parent_pid != current->pid) return (uint64_t)-ORE_EINVAL;
        if (child->state == ORE_PROC_READY) {
            current->wait_return_rip = syscall_user_rip_current();
            current->wait_return_rsp = syscall_user_rsp_current();
            current->waiting_for_pid = child->pid;
            current->state = ORE_PROC_BLOCKED;
            enter_process_from_syscall(child);
            kprintf("process: wait entering child pid %u entry 0x%lx stack 0x%lx\n",
                    child->pid, child->user_entry, child->user_stack);
            return 0;
        }
        return (uint64_t)child->exit_code;
    }
    if (number == ORE_SYS_GETPID) {
        Process *proc = process_current();
        return proc ? proc->pid : 0;
    }
    if (number == ORE_SYS_SLEEP) {
        uint64_t start = timer_ticks(0);
        while (timer_ticks(0) - start < a1) {
            __asm__ volatile("sti; hlt; cli");
        }
        return 0;
    }
    if (number == ORE_SYS_ARGS) {
        Process *proc = process_current();
        if (!proc) return (uint64_t)-ORE_EINVAL;
        if (!user_range_ok(a1, a2)) return (uint64_t)-ORE_EFAULT;
        uint64_t n = 0;
        while (n + 1 < a2 && proc->args[n]) {
            ((char *)(uintptr_t)a1)[n] = proc->args[n];
            n++;
        }
        if (a2) ((char *)(uintptr_t)a1)[n] = 0;
        return n;
    }
    if (number == ORE_SYS_EXEC_IMAGE) {
        if (!user_range_ok(a1, a2) || a2 < sizeof(OreImageHeader) || a2 > 1024 * 1024ULL) return (uint64_t)-ORE_EFAULT;
        char args_copy[128];
        int err = copy_user_string_optional(a3, args_copy, sizeof(args_copy));
        if (err < 0) return (uint64_t)err;
        uint8_t *image = kmalloc(a2);
        if (!image) return (uint64_t)-ORE_ENOMEM;
        err = copy_from_user(image, (const void *)(uintptr_t)a1, a2);
        if (err < 0) {
            kfree(image);
            return (uint64_t)err;
        }
        Process *child = process_create_user("ore-image");
        if (!child) {
            kfree(image);
            return (uint64_t)-ORE_ENOMEM;
        }
        err = load_ore_image_into_process(child, image, a2);
        kfree(image);
        if (err < 0) return (uint64_t)err;
        child->state = ORE_PROC_READY;
        child->exited = 0;
        child->exit_code = 0;
        child->parent_pid = process_current() ? process_current()->pid : 0;
        copy_string(child->args, args_copy, sizeof(child->args));
        kprintf("process: exec image pid %u entry 0x%lx\n", child->pid, child->user_entry);
        Process *current = process_current();
        if (!current) return child->pid;
        current->wait_return_rip = syscall_user_rip_current();
        current->wait_return_rsp = syscall_user_rsp_current();
        current->waiting_for_pid = child->pid;
        current->state = ORE_PROC_BLOCKED;
        enter_process_from_syscall(child);
        return 0;
    }
    if (number == ORE_SYS_PROC_INFO) {
        OreProcessInfo info;
        Process *proc = process_table_at((uint32_t)a1);
        if (!proc) return (uint64_t)-ORE_ENOENT;
        info.pid = proc->pid;
        info.ppid = proc->parent_pid;
        info.state = proc->state;
        info.exit_code = proc->exit_code;
        info.segment_count = proc->user_segment_count;
        info.syscall_count = proc->syscall_count;
        info.entry = proc->user_entry;
        info.stack = proc->user_stack;
        copy_string(info.name, proc->name ? proc->name : "(unnamed)", sizeof(info.name));
        int err = copy_to_user((void *)(uintptr_t)a2, &info, sizeof(info));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_MOD_INFO) {
        OreModuleInfo info;
        if (okmod_info((uint32_t)a1, &info) < 0) return (uint64_t)-ORE_ENOENT;
        int err = copy_to_user((void *)(uintptr_t)a2, &info, sizeof(info));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_KCMD) {
        const char *name = (const char *)(uintptr_t)a1;
        uint64_t len = 0;
        int err = strlen_user(name, 31, &len);
        if (err < 0) return (uint64_t)err;
        char name_copy[32];
        err = copy_from_user(name_copy, name, len + 1);
        if (err < 0) return (uint64_t)err;
        if (a3 && !user_range_ok(a2, a3)) return (uint64_t)-ORE_EFAULT;
        char *out = 0;
        uint64_t out_len = a3;
        if (out_len > 4096) return (uint64_t)-ORE_EINVAL;
        if (out_len) {
            out = kmalloc(out_len);
            if (!out) return (uint64_t)-ORE_ENOMEM;
        }
        int rc = okmod_run_command(name_copy, out, out_len);
        if (rc >= 0 && out && out_len) {
            out[out_len - 1] = 0;
            err = copy_to_user((void *)(uintptr_t)a2, out, out_len);
            kfree(out);
            if (err < 0) return (uint64_t)err;
        } else if (out) {
            kfree(out);
        }
        if (rc < 0) return (uint64_t)-ORE_ENOENT;
        return (uint64_t)(uint32_t)rc;
    }
    if (number == ORE_SYS_GFX_INFO) {
        OreGfxInfo info;
        int rc = console_gfx_info(&info);
        if (rc < 0) return (uint64_t)rc;
        int err = copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_GFX_PRESENT) {
        uint64_t len = a2;
        uint32_t width = (uint32_t)a3;
        uint32_t height = (uint32_t)a4;
        if (width != ORE_GFX_LOGICAL_WIDTH || height != ORE_GFX_LOGICAL_HEIGHT) return (uint64_t)-ORE_EINVAL;
        if (len != (uint64_t)width * height) return (uint64_t)-ORE_EINVAL;
        if (!user_range_ok(a1, len)) return (uint64_t)-ORE_EFAULT;
        int rc = console_gfx_present_indexed16((const uint8_t *)(uintptr_t)a1, width, height);
        return (uint64_t)rc;
    }
    if (number == ORE_SYS_INPUT_STATE) {
        OreInputState state;
        input_state_poll(&state);
        int err = copy_to_user((void *)(uintptr_t)a1, &state, sizeof(state));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_USER_ALLOC_PAGES) {
        return (uint64_t)user_alloc_pages_sys(a1);
    }
    if (number == ORE_SYS_USER_FREE_PAGES) {
        return (uint64_t)user_free_pages_sys(a1, a2);
    }
    if (number == ORE_SYS_TERRAIN_RENDER) {
        OreTerrainJob job;
        int err = copy_from_user(&job, (const void *)(uintptr_t)a1, sizeof(job));
        if (err < 0) return (uint64_t)err;
        if (a3 != (uint64_t)ORE_GFX_LOGICAL_WIDTH * ORE_GFX_LOGICAL_HEIGHT) return (uint64_t)-ORE_EINVAL;
        if (!user_range_ok(a2, a3) || !user_range_ok(a4, sizeof(OreTerrainResult))) return (uint64_t)-ORE_EFAULT;
        uint8_t *tmp = kmalloc(a3);
        if (!tmp) return (uint64_t)-ORE_ENOMEM;
        OreTerrainResult result;
        int rc = terrain_render_user(&job, tmp, a3, &result);
        if (rc >= 0) {
            err = copy_to_user((void *)(uintptr_t)a2, tmp, a3);
            if (err >= 0) err = copy_to_user((void *)(uintptr_t)a4, &result, sizeof(result));
        }
        kfree(tmp);
        if (rc < 0) return (uint64_t)rc;
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_NET_INFO) {
        OreNetInfo info;
        int rc = net_info(&info);
        if (rc < 0) return (uint64_t)rc;
        int err = copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
        if (err < 0) return (uint64_t)err;
        return 0;
    }
    if (number == ORE_SYS_NET_RECV) {
        if (a2 > ORE_NET_MAX_FRAME || !user_range_ok(a1, a2)) return (uint64_t)-ORE_EFAULT;
        uint8_t tmp[ORE_NET_MAX_FRAME];
        int64_t n = net_recv_frame(tmp, a2);
        if (n <= 0) return (uint64_t)n;
        int err = copy_to_user((void *)(uintptr_t)a1, tmp, (uint64_t)n);
        if (err < 0) return (uint64_t)err;
        return (uint64_t)n;
    }
    if (number == ORE_SYS_NET_SEND) {
        if (a2 > ORE_NET_MAX_FRAME || !user_range_ok(a1, a2)) return (uint64_t)-ORE_EFAULT;
        uint8_t tmp[ORE_NET_MAX_FRAME];
        int err = copy_from_user(tmp, (const void *)(uintptr_t)a1, a2);
        if (err < 0) return (uint64_t)err;
        return (uint64_t)net_send_frame(tmp, a2);
    }
    kprintf("user: invalid syscall 0x%lx\n", number);
    return (uint64_t)-ORE_ENOSYS;
}
