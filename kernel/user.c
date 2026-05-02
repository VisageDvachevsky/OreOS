#include "kernel.h"

#define PAGE_SIZE 4096ULL
#define PT_LOAD 1
#define USER_STACK_TOP 0x0000009000000000ULL
#define USER_STACK_PAGES 8
#define USER_FILE_MAX (512 * 1024ULL)

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint64_t start;
    uint64_t size;
    uint64_t flags;
} LoadedSegment;

static uint64_t align_down(uint64_t value) {
    return value & ~(PAGE_SIZE - 1);
}

static uint64_t align_up(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static int valid_elf(const Elf64_Ehdr *eh, uint64_t size) {
    return size >= sizeof(*eh) &&
           eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' &&
           eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F' &&
           eh->e_ident[4] == 2 && eh->e_machine == 62 &&
           eh->e_phoff + (uint64_t)eh->e_phentsize * eh->e_phnum <= size;
}

static uint64_t flags_for_segment(const Elf64_Phdr *ph) {
    uint64_t flags = VMM_USER;
    if (ph->p_flags & 2) flags |= VMM_WRITE;
    return flags;
}

static void map_segment(Process *proc, const uint8_t *image, const Elf64_Phdr *ph, LoadedSegment *out) {
    uint64_t start = align_down(ph->p_vaddr);
    uint64_t end = align_up(ph->p_vaddr + ph->p_memsz);
    uint64_t flags = VMM_USER | VMM_WRITE;
    for (uint64_t va = start; va < end; va += PAGE_SIZE) {
        if (!vmm_virt_to_phys_in(proc->address_space.pml4_phys, va)) {
            void *page = pmm_alloc_page();
            if (!page) panic("user: no memory for segment");
            if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)page, PAGE_SIZE, flags) < 0) {
                panic("user: map segment failed");
            }
        }
    }
    for (uint64_t off = 0; off < ph->p_memsz; ++off) {
        uint64_t phys = vmm_virt_to_phys_in(proc->address_space.pml4_phys, ph->p_vaddr + off);
        if (!phys) panic("user: segment translation failed");
        *(uint8_t *)(uintptr_t)phys = 0;
    }
    for (uint64_t off = 0; off < ph->p_filesz; ++off) {
        uint64_t phys = vmm_virt_to_phys_in(proc->address_space.pml4_phys, ph->p_vaddr + off);
        if (!phys) panic("user: segment copy translation failed");
        *(uint8_t *)(uintptr_t)phys = image[ph->p_offset + off];
    }
    out->start = start;
    out->size = end - start;
    out->flags = flags_for_segment(ph);
    kprintf("user: segment va 0x%lx size 0x%lx flags 0x%lx\n", out->start, out->size, out->flags);
}

static uint64_t map_user_stack(Process *proc) {
    uint64_t base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t va = base; va < USER_STACK_TOP; va += PAGE_SIZE) {
        void *page = pmm_alloc_page();
        if (!page) panic("user: no stack memory");
        if (vmm_map_in(proc->address_space.pml4_phys, va, (uint64_t)(uintptr_t)page, PAGE_SIZE, VMM_USER | VMM_WRITE | VMM_NX) < 0) {
            panic("user: map stack failed");
        }
    }
    return USER_STACK_TOP - 16;
}

void user_init_spawn(const char *path) {
    kprintf("user: loading %s\n", path);
    Process *proc = process_create_user(path);
    if (!proc) panic("user: cannot create init process");
    process_set_current(proc);
    VfsStat st;
    if (vfs_stat(path, &st) < 0) panic("user: /init missing");
    if (st.size == 0 || st.size > USER_FILE_MAX) panic("user: /init invalid size");
    uint8_t *image = kmalloc(st.size);
    if (!image) panic("user: no memory for init image");
    int64_t n = vfs_read_file(path, image, st.size);
    if (n != (int64_t)st.size) panic("user: cannot read /init");
    Elf64_Ehdr *eh = (Elf64_Ehdr *)image;
    if (!valid_elf(eh, st.size)) panic("user: invalid /init ELF");
    UserElfInfo exec_info;
    if (elf_probe_user_image(image, st.size, &exec_info) < 0) panic("user: /init ELF probe failed");
    proc->user_entry = exec_info.entry;
    proc->user_segment_count = exec_info.segment_count;
    LoadedSegment segments[16];
    uint16_t loaded = 0;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(image + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type == PT_LOAD && ph->p_memsz) {
            if (loaded >= 16) panic("user: too many ELF segments");
            map_segment(proc, image, ph, &segments[loaded++]);
        }
    }
    for (uint16_t i = 0; i < loaded; ++i) {
        if (vmm_protect_in(proc->address_space.pml4_phys, segments[i].start, segments[i].size, segments[i].flags) < 0) {
            panic("user: protect segment failed");
        }
    }
    uint64_t user_stack = map_user_stack(proc);
    proc->user_stack = user_stack;
    if (!vmm_virt_to_phys_in(proc->address_space.pml4_phys, eh->e_entry) ||
        !vmm_virt_to_phys_in(proc->address_space.pml4_phys, user_stack)) {
        panic("user: entry or stack not mapped");
    }
    write_cr3(proc->address_space.pml4_phys);
    if ((vmm_flags(eh->e_entry) & VMM_USER) == 0 || (vmm_flags(user_stack) & VMM_USER) == 0) {
        panic("user: entry or stack missing user flag");
    }
    uint8_t *kernel_stack = pmm_alloc_pages(8);
    if (!kernel_stack) panic("user: no rsp0 stack");
    tss_set_rsp0((uint64_t)(uintptr_t)kernel_stack + 8 * PAGE_SIZE);
    kprintf("user: entered ring3 entry 0x%lx stack 0x%lx\n", eh->e_entry, user_stack);
    user_enter(eh->e_entry, user_stack);
}
