#include <kern/mm.h>
#include <kern/string.h>

/* One bit tracks each 4 KiB frame below the 128 GiB address limit. */
#define PMM_MAX_PHYS (128ULL * 1024 * 1024 * 1024)
#define PMM_MAX_PAGES (PMM_MAX_PHYS / PAGE_SIZE)
#define PMM_BITMAP_BYTES (PMM_MAX_PAGES / 8)
static uint8_t frame_bitmap[PMM_BITMAP_BYTES];
static uint8_t allocated_bitmap[PMM_BITMAP_BYTES];
static size_t tracked_pages;
static size_t free_pages;

extern char __kernel_start[];
extern char __kernel_end[];

static bool frame_used(size_t pfn) {
    return (frame_bitmap[pfn >> 3] & (1u << (pfn & 7))) != 0;
}
static bool frame_allocated(size_t pfn) {
    return (allocated_bitmap[pfn >> 3] & (1u << (pfn & 7))) != 0;
}
static void mark_frame(size_t pfn, bool used) {
    uint8_t mask = (uint8_t)(1u << (pfn & 7));
    if (used) frame_bitmap[pfn >> 3] |= mask;
    else frame_bitmap[pfn >> 3] &= (uint8_t)~mask;
}
static void mark_allocated(size_t pfn, bool allocated) {
    uint8_t mask = (uint8_t)(1u << (pfn & 7));
    if (allocated) allocated_bitmap[pfn >> 3] |= mask;
    else allocated_bitmap[pfn >> 3] &= (uint8_t)~mask;
}
static void set_range(uint64_t base, uint64_t length, bool used) {
    if (!length || base >= PMM_MAX_PHYS) return;
    uint64_t end = base + length;
    if (end < base) return;
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;
    /* Free only complete usable frames; reserve every frame touched by a range. */
    size_t first = (size_t)((used ? ALIGN_DOWN(base, PAGE_SIZE) : ALIGN_UP(base, PAGE_SIZE)) / PAGE_SIZE);
    size_t last = (size_t)((used ? ALIGN_UP(end, PAGE_SIZE) : ALIGN_DOWN(end, PAGE_SIZE)) / PAGE_SIZE);
    if (last > tracked_pages) last = tracked_pages;
    for (size_t pfn = first; pfn < last; ++pfn) {
        bool was_used = frame_used(pfn);
        if (was_used && !used) { mark_frame(pfn, false); ++free_pages; }
        else if (!was_used && used) { mark_frame(pfn, true); --free_pages; }
    }
}

void pmm_init(const struct multiboot2_mmap_tag *map, size_t map_size,
              uintptr_t boot_info_address, size_t boot_info_size) {
    memset(frame_bitmap, 0xff, sizeof(frame_bitmap));
    memset(allocated_bitmap, 0, sizeof(allocated_bitmap));
    tracked_pages = PMM_MAX_PAGES;
    free_pages = 0;
    if (!map || map->type != MULTIBOOT2_TAG_MEMORY_MAP ||
        map_size != map->size || map_size < sizeof(*map) ||
        map->entry_version != 0 ||
        map->entry_size < sizeof(struct multiboot2_mmap_entry) ||
        map_size - sizeof(*map) < map->entry_size ||
        (map_size - sizeof(*map)) % map->entry_size != 0)
        return;
    size_t offset = sizeof(*map);
    while (offset + map->entry_size <= map_size) {
        const struct multiboot2_mmap_entry *entry =
            (const void *)((const uint8_t *)map + offset);
        if (entry->type == 1) set_range(entry->base, entry->length, false);
        offset += map->entry_size;
    }
    /* Reserve these ranges even if firmware marks them as usable. */
    set_range(0, 1024 * 1024, true);
    set_range((uintptr_t)__kernel_start,
              (uintptr_t)__kernel_end - (uintptr_t)__kernel_start, true);
    set_range(boot_info_address, boot_info_size, true);
}

phys_addr_t pmm_alloc_pages(size_t count, phys_addr_t max_address) {
    if (!count || !tracked_pages) return 0;
    size_t limit = tracked_pages;
    if (max_address && max_address / PAGE_SIZE < limit) limit = max_address / PAGE_SIZE;
    size_t run = 0, start = 1;
    for (size_t pfn = 1; pfn < limit; ++pfn) {
        if (!frame_used(pfn)) {
            if (!run) start = pfn;
            if (++run == count) {
                for (size_t i = start; i < start + count; ++i) {
                    mark_frame(i, true);
                    mark_allocated(i, true);
                }
                free_pages -= count;
                return (phys_addr_t)start * PAGE_SIZE;
            }
        } else run = 0;
    }
    return 0;
}

void pmm_free_pages(phys_addr_t base, size_t count) {
    if ((base & PAGE_MASK) || !count) return;
    size_t first = base / PAGE_SIZE;
    if (first >= tracked_pages || count > tracked_pages - first) return;
    for (size_t i = 0; i < count; ++i) {
        if (!frame_used(first + i) || !frame_allocated(first + i))
            return; /* Reject invalid or repeated frees without changing any frame. */
    }
    set_range(base, count * PAGE_SIZE, false);
    for (size_t i = 0; i < count; ++i) mark_allocated(first + i, false);
}

size_t pmm_total_pages(void) { return tracked_pages; }
size_t pmm_free_page_count(void) { return free_pages; }
