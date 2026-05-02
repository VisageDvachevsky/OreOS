#include "kernel.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define PTE_ADDR_MASK 0x000ffffffffff000ULL
#define USER_BASE 0x0000008000000000ULL
#define USER_LIMIT 0x0000800000000000ULL

static uint64_t *kernel_pml4;
static uint64_t kernel_pml4_phys;

static uint64_t align_down(uint64_t value) {
    return value & PAGE_MASK;
}

static uint64_t align_up(uint64_t value) {
    return (value + PAGE_SIZE - 1) & PAGE_MASK;
}

static void mem_zero_local(void *ptr, uint64_t size) {
    uint8_t *p = ptr;
    for (uint64_t i = 0; i < size; ++i) p[i] = 0;
}

static uint64_t *new_table(void) {
    uint64_t *table = (uint64_t *)pmm_alloc_page();
    if (!table) panic("VMM: no page table memory");
    mem_zero_local(table, PAGE_SIZE);
    return table;
}

static uint64_t *next_table(uint64_t *table, uint64_t index, int create, uint64_t create_flags) {
    if (!(table[index] & VMM_PRESENT)) {
        if (!create) return 0;
        uint64_t *child = new_table();
        table[index] = ((uint64_t)(uintptr_t)child & PTE_ADDR_MASK) | VMM_PRESENT | VMM_WRITE | create_flags;
    }
    return (uint64_t *)(uintptr_t)(table[index] & PTE_ADDR_MASK);
}

static uint64_t *pte_for_root(uint64_t *root, uint64_t virt, int create, uint64_t create_flags) {
    uint64_t pml4_i = (virt >> 39) & 0x1ff;
    uint64_t pdpt_i = (virt >> 30) & 0x1ff;
    uint64_t pd_i = (virt >> 21) & 0x1ff;
    uint64_t pt_i = (virt >> 12) & 0x1ff;

    uint64_t *pdpt = next_table(root, pml4_i, create, create_flags);
    if (!pdpt) return 0;
    uint64_t *pd = next_table(pdpt, pdpt_i, create, create_flags);
    if (!pd) return 0;
    uint64_t *pt = next_table(pd, pd_i, create, create_flags);
    if (!pt) return 0;
    return &pt[pt_i];
}

static int map_in_root(uint64_t *root, uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    uint64_t start = align_down(virt);
    uint64_t end = align_up(virt + size);
    uint64_t phys_page = align_down(phys);
    uint64_t create_flags = flags & VMM_USER;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE, phys_page += PAGE_SIZE) {
        uint64_t *pte = pte_for_root(root, addr, 1, create_flags);
        if (!pte) return -1;
        *pte = (phys_page & PTE_ADDR_MASK) | (flags & ~VMM_NX) | VMM_PRESENT | (flags & VMM_NX);
        __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
    return 0;
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    return map_in_root(kernel_pml4, virt, phys, size, flags);
}

int vmm_map_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    return map_in_root((uint64_t *)(uintptr_t)pml4_phys, virt, phys, size, flags);
}

static int protect_in_root(uint64_t *root, uint64_t virt, uint64_t size, uint64_t flags) {
    uint64_t start = align_down(virt);
    uint64_t end = align_up(virt + size);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t *pte = pte_for_root(root, addr, 0, 0);
        if (!pte || !(*pte & VMM_PRESENT)) return -1;
        uint64_t phys = *pte & PTE_ADDR_MASK;
        *pte = phys | (flags & ~VMM_NX) | VMM_PRESENT | (flags & VMM_NX);
        __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
    }
    return 0;
}

int vmm_protect(uint64_t virt, uint64_t size, uint64_t flags) {
    return protect_in_root(kernel_pml4, virt, size, flags);
}

int vmm_protect_in(uint64_t pml4_phys, uint64_t virt, uint64_t size, uint64_t flags) {
    return protect_in_root((uint64_t *)(uintptr_t)pml4_phys, virt, size, flags);
}

static void unmap_in_root(uint64_t *root, uint64_t virt, uint64_t size) {
    uint64_t start = align_down(virt);
    uint64_t end = align_up(virt + size);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t *pte = pte_for_root(root, addr, 0, 0);
        if (pte) {
            *pte = 0;
            __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
        }
    }
}

void vmm_unmap(uint64_t virt, uint64_t size) {
    unmap_in_root(kernel_pml4, virt, size);
}

void vmm_unmap_in(uint64_t pml4_phys, uint64_t virt, uint64_t size) {
    unmap_in_root((uint64_t *)(uintptr_t)pml4_phys, virt, size);
}

uint64_t vmm_virt_to_phys(uint64_t virt) {
    return vmm_virt_to_phys_in(read_cr3(), virt);
}

uint64_t vmm_virt_to_phys_in(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pte = pte_for_root((uint64_t *)(uintptr_t)pml4_phys, virt, 0, 0);
    if (!pte || !(*pte & VMM_PRESENT)) return 0;
    return (*pte & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

uint64_t vmm_flags(uint64_t virt) {
    uint64_t *pte = pte_for_root((uint64_t *)(uintptr_t)read_cr3(), virt, 0, 0);
    if (!pte || !(*pte & VMM_PRESENT)) return 0;
    return *pte & ~PTE_ADDR_MASK;
}

uint64_t vmm_kernel_pml4(void) {
    return kernel_pml4_phys;
}

uint64_t vmm_create_user_pml4(void) {
    uint64_t *root = new_table();
    for (uint32_t i = 0; i < 512; ++i) root[i] = kernel_pml4[i];
    for (uint32_t i = 1; i < 256; ++i) root[i] = 0;
    return (uint64_t)(uintptr_t)root;
}

void vmm_init(const BootInfo *boot_info) {
    kernel_pml4 = new_table();
    kernel_pml4_phys = (uint64_t)(uintptr_t)kernel_pml4;

    uint64_t phys_limit = pmm_total_pages() * PAGE_SIZE;
    if (vmm_map(0, 0, phys_limit, VMM_WRITE) < 0) panic("VMM: identity map failed");
    if (boot_info->framebuffer.base && boot_info->framebuffer.width && boot_info->framebuffer.height) {
        uint64_t fb_size = (uint64_t)boot_info->framebuffer.pixels_per_scanline * boot_info->framebuffer.height * 4ULL;
        if (vmm_map(boot_info->framebuffer.base, boot_info->framebuffer.base, fb_size, VMM_WRITE | VMM_NX) < 0) {
            panic("VMM: framebuffer map failed");
        }
    }
    if (acpi_lapic_base()) {
        if (vmm_map(acpi_lapic_base(), acpi_lapic_base(), PAGE_SIZE, VMM_WRITE | VMM_NX) < 0) {
            panic("VMM: LAPIC map failed");
        }
    }

    write_cr3(kernel_pml4_phys);
    kprintf("VMM: kernel page tables online cr3 0x%lx user 0x%lx-0x%lx\n",
            kernel_pml4_phys, USER_BASE, USER_LIMIT);
}

void vmm_self_test(const BootInfo *boot_info) {
    if (vmm_virt_to_phys(boot_info->kernel_base) != boot_info->kernel_base) {
        panic("VMM: kernel identity translation failed");
    }
    void *page = pmm_alloc_page();
    if (!page) panic("VMM: self-test alloc failed");
    uint64_t addr = (uint64_t)(uintptr_t)page;
    if (vmm_map(addr, addr, PAGE_SIZE, VMM_WRITE | VMM_NX) < 0) panic("VMM: self-test map failed");
    if (vmm_virt_to_phys(addr) != addr) panic("VMM: self-test translation failed");
    vmm_unmap(addr, PAGE_SIZE);
    if (vmm_virt_to_phys(addr) != 0) panic("VMM: self-test unmap failed");
    if (vmm_map(addr, addr, PAGE_SIZE, VMM_WRITE | VMM_NX) < 0) panic("VMM: self-test remap failed");
    pmm_free_page(page);
    kprintf("VMM: self-test ok\n");
}
