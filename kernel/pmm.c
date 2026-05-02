#include "kernel.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define LOW_RESERVED_END 0x100000ULL
#define TRAMPOLINE_START 0x7000ULL
#define TRAMPOLINE_END 0x9000ULL

static Spinlock pmm_lock;
static uint8_t *bitmap;
static uint64_t bitmap_bytes;
static uint64_t max_phys;
static uint64_t total_page_count;
static uint64_t free_page_count;
static uint64_t reserved_page_count;
static uint64_t used_page_count;

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static void mem_zero(void *ptr, uint64_t size) {
    uint8_t *p = ptr;
    for (uint64_t i = 0; i < size; ++i) p[i] = 0;
}

static void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (uint8_t)(1u << (page % 8));
}

static void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= (uint8_t)~(1u << (page % 8));
}

static int bitmap_test(uint64_t page) {
    return (bitmap[page / 8] & (uint8_t)(1u << (page % 8))) != 0;
}

static int ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end) {
    return a_start < b_end && b_start < a_end;
}

static int usable_descriptor(const OreMemoryDescriptor *d) {
    return d->type == 7;
}

static void reserve_range(uint64_t start, uint64_t end) {
    start = align_down(start, PAGE_SIZE);
    end = align_up(end, PAGE_SIZE);
    for (uint64_t addr = start; addr < end && addr < max_phys; addr += PAGE_SIZE) {
        uint64_t page = addr / PAGE_SIZE;
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (free_page_count) free_page_count--;
            reserved_page_count++;
        }
    }
}

static void find_bitmap_storage(const BootInfo *boot_info, uint64_t bytes, uint64_t *out_addr) {
    uint64_t pages = align_up(bytes, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t i = 0; i < boot_info->memory_map_entries; ++i) {
        const OreMemoryDescriptor *d = &boot_info->memory_map[i];
        if (!usable_descriptor(d)) continue;
        uint64_t start = align_up(d->physical_start, PAGE_SIZE);
        uint64_t end = align_down(d->physical_start + d->number_of_pages * PAGE_SIZE, PAGE_SIZE);
        if (start < LOW_RESERVED_END) start = LOW_RESERVED_END;
        for (uint64_t addr = start; addr + pages * PAGE_SIZE <= end; addr += PAGE_SIZE) {
            if (ranges_overlap(addr, addr + pages * PAGE_SIZE, boot_info->kernel_base, boot_info->kernel_base + boot_info->kernel_size)) continue;
            *out_addr = addr;
            return;
        }
    }
    panic("PMM: no room for bitmap");
}

void pmm_init(const BootInfo *boot_info) {
    max_phys = 0;
    for (uint64_t i = 0; i < boot_info->memory_map_entries; ++i) {
        const OreMemoryDescriptor *d = &boot_info->memory_map[i];
        if (!usable_descriptor(d)) continue;
        uint64_t end = d->physical_start + d->number_of_pages * PAGE_SIZE;
        if (end > max_phys) max_phys = end;
    }
    if (!max_phys) panic("PMM: no usable memory");

    total_page_count = align_up(max_phys, PAGE_SIZE) / PAGE_SIZE;
    bitmap_bytes = align_up((total_page_count + 7) / 8, PAGE_SIZE);
    uint64_t bitmap_addr = 0;
    find_bitmap_storage(boot_info, bitmap_bytes, &bitmap_addr);
    bitmap = (uint8_t *)(uintptr_t)bitmap_addr;
    for (uint64_t i = 0; i < bitmap_bytes; ++i) bitmap[i] = 0xff;

    free_page_count = 0;
    reserved_page_count = total_page_count;
    used_page_count = 0;

    for (uint64_t i = 0; i < boot_info->memory_map_entries; ++i) {
        const OreMemoryDescriptor *d = &boot_info->memory_map[i];
        if (!usable_descriptor(d)) continue;
        uint64_t start = align_up(d->physical_start, PAGE_SIZE);
        uint64_t end = align_down(d->physical_start + d->number_of_pages * PAGE_SIZE, PAGE_SIZE);
        for (uint64_t addr = start; addr + PAGE_SIZE <= end; addr += PAGE_SIZE) {
            uint64_t page = addr / PAGE_SIZE;
            bitmap_clear(page);
            free_page_count++;
            reserved_page_count--;
        }
    }

    reserve_range(0, LOW_RESERVED_END);
    reserve_range(TRAMPOLINE_START, TRAMPOLINE_END);
    reserve_range(bitmap_addr, bitmap_addr + bitmap_bytes);
    reserve_range(boot_info->kernel_base, boot_info->kernel_base + boot_info->kernel_size);
    if (boot_info->initramfs_base && boot_info->initramfs_size) {
        reserve_range(boot_info->initramfs_base, boot_info->initramfs_base + boot_info->initramfs_size);
    }
    reserve_range((uint64_t)(uintptr_t)boot_info, (uint64_t)(uintptr_t)boot_info + sizeof(BootInfo));

    kprintf("PMM: bitmap at 0x%lx bytes 0x%lx total 0x%lx free 0x%lx reserved 0x%lx\n",
            bitmap_addr, bitmap_bytes, total_page_count, free_page_count, reserved_page_count);
}

void *pmm_alloc_pages(uint32_t count) {
    if (!count) return 0;
    spinlock_lock(&pmm_lock);
    uint64_t run = 0;
    uint64_t start_page = 0;
    for (uint64_t page = LOW_RESERVED_END / PAGE_SIZE; page < total_page_count; ++page) {
        if (!bitmap_test(page)) {
            if (run == 0) start_page = page;
            run++;
            if (run == count) {
                for (uint64_t i = 0; i < count; ++i) bitmap_set(start_page + i);
                free_page_count -= count;
                used_page_count += count;
                spinlock_unlock(&pmm_lock);
                void *ptr = (void *)(uintptr_t)(start_page * PAGE_SIZE);
                mem_zero(ptr, count * PAGE_SIZE);
                return ptr;
            }
        } else {
            run = 0;
        }
    }
    spinlock_unlock(&pmm_lock);
    return 0;
}

void pmm_free_pages(void *ptr, uint32_t count) {
    if (!ptr || !count) return;
    uint64_t page = (uint64_t)(uintptr_t)ptr / PAGE_SIZE;
    spinlock_lock(&pmm_lock);
    for (uint32_t i = 0; i < count; ++i) {
        if (page + i < total_page_count && bitmap_test(page + i)) {
            bitmap_clear(page + i);
            free_page_count++;
            if (used_page_count) used_page_count--;
        }
    }
    spinlock_unlock(&pmm_lock);
}

void *pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

void pmm_free_page(void *page) {
    pmm_free_pages(page, 1);
}

void *early_alloc_pages(uint32_t pages) {
    return pmm_alloc_pages(pages);
}

uint64_t pmm_total_pages(void) {
    return total_page_count;
}

uint64_t pmm_free_pages_count(void) {
    return free_page_count;
}

uint64_t pmm_used_pages(void) {
    return used_page_count;
}

uint64_t pmm_reserved_pages(void) {
    return reserved_page_count;
}
