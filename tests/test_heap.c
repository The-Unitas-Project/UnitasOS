#include <assert.h>
#include <kern/mm.h>
#include <kern/string.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t arena[16 * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static bool arena_used;

phys_addr_t pmm_alloc_pages(size_t count, phys_addr_t max_address) {
    (void)max_address;
    if (arena_used || count > 16) return 0;
    arena_used = true;
    return (phys_addr_t)(uintptr_t)arena;
}

int main(void) {
    heap_init();
    assert(kmalloc(SIZE_MAX) == 0);
    assert(kmalloc(SIZE_MAX - 7) == 0);
    assert(kcalloc(SIZE_MAX, 2) == 0);

    uint8_t *memory = kmalloc(24);
    assert(memory);
    memset(memory, 0x5a, 24);
    uint8_t *larger = krealloc(memory, 128);
    assert(larger);
    for (size_t i = 0; i < 24; ++i) assert(larger[i] == 0x5a);
    kfree(larger);
    puts("heap tests passed");
    return 0;
}
