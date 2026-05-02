#include "kernel.h"

static int aligned_16(void *ptr) {
    return (((uint64_t)(uintptr_t)ptr) & 0xf) == 0;
}

void memory_self_test(void) {
    void *page = pmm_alloc_page();
    if (!page) panic("memory test: pmm_alloc_page failed");
    pmm_free_page(page);

    void *pages = pmm_alloc_pages(4);
    if (!pages) panic("memory test: pmm_alloc_pages failed");
    if (((uint64_t)(uintptr_t)pages & 0xfff) != 0) panic("memory test: pages unaligned");
    pmm_free_pages(pages, 4);

    void *a = kmalloc(24);
    void *b = kmalloc(128);
    void *c = kmalloc(3900);
    if (!a || !b || !c) panic("memory test: kmalloc failed");
    if (!aligned_16(a) || !aligned_16(b) || !aligned_16(c)) panic("memory test: kmalloc alignment");
    kfree(b);
    void *d = kmalloc(64);
    if (!d || !aligned_16(d)) panic("memory test: kmalloc reuse failed");

    uint8_t *z = kzalloc(96);
    if (!z) panic("memory test: kzalloc failed");
    for (uint32_t i = 0; i < 96; ++i) {
        if (z[i] != 0) panic("memory test: kzalloc nonzero");
    }

    kfree(a);
    kfree(c);
    kfree(d);
    kfree(z);
    kprintf("memory: self-test ok total 0x%lx free 0x%lx used 0x%lx reserved 0x%lx\n",
            pmm_total_pages(), pmm_free_pages_count(), pmm_used_pages(), pmm_reserved_pages());
}
