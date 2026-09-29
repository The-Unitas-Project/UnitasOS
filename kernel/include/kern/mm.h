#ifndef UNITAS_KERN_MM_H
#define UNITAS_KERN_MM_H

#include <kern/boot.h>

void pmm_init(const struct multiboot2_mmap_tag *map, size_t map_size,
              uintptr_t boot_info_address, size_t boot_info_size);
/*
 * Allocate count contiguous pages. Do not use addresses at or above max_address.
 * Set max_address to 0 for no limit. Return the physical base or 0 on failure.
 */
phys_addr_t pmm_alloc_pages(size_t count, phys_addr_t max_address);
/* Frees pages returned by pmm_alloc_pages; reserved frames are rejected. */
void pmm_free_pages(phys_addr_t base, size_t count);
size_t pmm_total_pages(void);
size_t pmm_free_page_count(void);
void heap_init(void);
/* Heap pointers are identity-mapped kernel addresses; allocation can fail. */
void *kmalloc(size_t size);
/* kcalloc checks count * size for overflow and clears a successful allocation. */
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

#endif
