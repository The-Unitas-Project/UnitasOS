#include <assert.h>
#include <kern/mm.h>
#include <stdio.h>

char __kernel_start[1];
char __kernel_end[1];

struct test_map {
    struct multiboot2_mmap_tag tag;
    struct multiboot2_mmap_entry entries[2];
} __attribute__((packed));

int main(void) {
    struct test_map map = {
        .tag = {
            .type = MULTIBOOT2_TAG_MEMORY_MAP,
            .size = sizeof(map),
            .entry_size = sizeof(struct multiboot2_mmap_entry),
            .entry_version = 0
        },
        .entries = {
            { .base = 0, .length = 64 * 1024 * 1024, .type = 1 },
            { .base = 64 * 1024 * 1024, .length = UINT64_MAX, .type = 1 }
        }
    };

    pmm_init(&map.tag, sizeof(map), 0, 0);
    assert(pmm_alloc_pages(20000, 128 * 1024 * 1024) == 0);
    pmm_free_pages(0x1000, 1);
    assert(pmm_alloc_pages(1, 0x40000000) == 0x100000);

    size_t before = pmm_free_page_count();
    phys_addr_t pages = pmm_alloc_pages(2, 0x40000000);
    assert(pages != 0);
    assert(pmm_free_page_count() == before - 2);
    pmm_free_pages(pages, 2);
    assert(pmm_free_page_count() == before);
    pmm_free_pages(pages, 2);
    assert(pmm_free_page_count() == before);
    puts("PMM tests passed");
    return 0;
}
