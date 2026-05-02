#include "kernel.h"

#define PAGE_SIZE 4096ULL
#define HEAP_MAGIC 0x484541504f52454ULL

typedef struct HeapBlock {
    uint64_t magic;
    uint64_t size;
    uint32_t free;
    struct HeapBlock *next;
} HeapBlock;

static Spinlock heap_lock;
static HeapBlock *heap_head;

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static void mem_zero(void *ptr, uint64_t size) {
    uint8_t *p = ptr;
    for (uint64_t i = 0; i < size; ++i) p[i] = 0;
}

void heap_init(void) {
    heap_head = 0;
    kprintf("heap: initialized\n");
}

static HeapBlock *request_block(uint64_t size) {
    uint64_t total = align_up(sizeof(HeapBlock) + size, PAGE_SIZE);
    uint32_t pages = (uint32_t)(total / PAGE_SIZE);
    HeapBlock *block = (HeapBlock *)pmm_alloc_pages(pages);
    if (!block) return 0;
    block->magic = HEAP_MAGIC;
    block->size = total - sizeof(HeapBlock);
    block->free = 1;
    block->next = 0;
    if (!heap_head) {
        heap_head = block;
    } else {
        HeapBlock *tail = heap_head;
        while (tail->next) tail = tail->next;
        tail->next = block;
    }
    return block;
}

static void split_block(HeapBlock *block, uint64_t size) {
    uint64_t remaining = block->size - size;
    if (remaining <= sizeof(HeapBlock) + 32) return;
    HeapBlock *next = (HeapBlock *)((uint8_t *)(block + 1) + size);
    next->magic = HEAP_MAGIC;
    next->size = remaining - sizeof(HeapBlock);
    next->free = 1;
    next->next = block->next;
    block->size = size;
    block->next = next;
}

void *kmalloc(uint64_t size) {
    if (!size) return 0;
    size = align_up(size, 16);
    spinlock_lock(&heap_lock);
    HeapBlock *block = heap_head;
    while (block) {
        if (block->magic == HEAP_MAGIC && block->free && block->size >= size) break;
        block = block->next;
    }
    if (!block) block = request_block(size);
    if (!block) {
        spinlock_unlock(&heap_lock);
        return 0;
    }
    split_block(block, size);
    block->free = 0;
    void *ptr = block + 1;
    spinlock_unlock(&heap_lock);
    return ptr;
}

void *kzalloc(uint64_t size) {
    void *ptr = kmalloc(size);
    if (ptr) mem_zero(ptr, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    HeapBlock *block = ((HeapBlock *)ptr) - 1;
    if (block->magic != HEAP_MAGIC) panic("heap: invalid free");
    spinlock_lock(&heap_lock);
    block->free = 1;
    for (HeapBlock *cur = heap_head; cur && cur->next; cur = cur->next) {
        if (cur->free && cur->next->free &&
            (uint8_t *)(cur + 1) + cur->size == (uint8_t *)cur->next) {
            cur->size += sizeof(HeapBlock) + cur->next->size;
            cur->next = cur->next->next;
        }
    }
    spinlock_unlock(&heap_lock);
}
