#include <kern/mm.h>
#include <kern/string.h>
#include <stdint.h>

#define HEAP_REGION_PAGES 16
#define HEAP_MAX_PHYS (1024ULL * 1024 * 1024)

/* Blocks remain in address order so adjacent free blocks can be coalesced. */
struct heap_block {
    size_t size;                 /* Payload size. The header is stored separately. */
    bool free;
    struct heap_block *previous;
    struct heap_block *next;
};
static struct heap_block *blocks;
static const size_t header_size = ALIGN_UP(sizeof(struct heap_block), 16);

static bool add_region(size_t requested) {
    size_t pages = (requested + header_size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < HEAP_REGION_PAGES) pages = HEAP_REGION_PAGES;
    phys_addr_t physical = pmm_alloc_pages(pages, HEAP_MAX_PHYS);
    if (!physical) return false;
    struct heap_block *block = (void *)(uintptr_t)physical;
    block->size = pages * PAGE_SIZE - header_size;
    block->free = true;
    block->previous = 0;
    block->next = blocks;
    if (blocks) blocks->previous = block;
    blocks = block;
    return true;
}

void heap_init(void) { (void)add_region(0); }

void *kmalloc(size_t size) {
    if (!size) size = 1;
    size = ALIGN_UP(size, 16);
    for (;;) {
        for (struct heap_block *block = blocks; block; block = block->next) {
            if (!block->free || block->size < size) continue;
            if (block->size >= size + header_size + 16) {
                struct heap_block *split = (void *)((uintptr_t)block + header_size + size);
                split->size = block->size - size - header_size;
                split->free = true;
                split->previous = block;
                split->next = block->next;
                if (split->next) split->next->previous = split;
                block->next = split;
                block->size = size;
            }
            block->free = false;
            return (void *)((uintptr_t)block + header_size);
        }
        if (!add_region(size)) return 0;
    }
}

void *kcalloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return 0;
    size_t total = count * size;
    void *memory = kmalloc(total);
    if (memory) memset(memory, 0, total);
    return memory;
}

void kfree(void *pointer) {
    if (!pointer) return;
    struct heap_block *block = (void *)((uintptr_t)pointer - header_size);
    if (block->free) return;
    block->free = true;
    if (block->next && block->next->free &&
        (uintptr_t)block + header_size + block->size == (uintptr_t)block->next) {
        struct heap_block *next = block->next;
        block->size += header_size + next->size;
        block->next = next->next;
        if (block->next) block->next->previous = block;
    }
    if (block->previous && block->previous->free &&
        (uintptr_t)block->previous + header_size + block->previous->size == (uintptr_t)block) {
        struct heap_block *previous = block->previous;
        previous->size += header_size + block->size;
        previous->next = block->next;
        if (previous->next) previous->next->previous = previous;
    }
}

void *krealloc(void *pointer, size_t size) {
    if (!pointer) return kmalloc(size);
    if (!size) { kfree(pointer); return 0; }
    struct heap_block *block = (void *)((uintptr_t)pointer - header_size);
    if (block->size >= size) return pointer;
    void *replacement = kmalloc(size);
    if (!replacement) return 0;
    memcpy(replacement, pointer, block->size);
    kfree(pointer);
    return replacement;
}
