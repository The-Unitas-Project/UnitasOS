#ifndef UNITAS_KERN_MM_H
#define UNITAS_KERN_MM_H

#include <kern/boot.h>

void pmm_init(const struct multiboot2_mmap_tag *map, size_t map_size,
              uintptr_t boot_info_address, size_t boot_info_size);
/* Apply kernel page permissions. Return -1 if setup fails or NX is absent. */
int paging_init(void);
/* Allocate count contiguous frames below max_address. Return zero on failure.
 * Set max_address to zero for no bound. Use a 4 GiB limit for identity access.
 */
phys_addr_t pmm_alloc_pages(size_t count, phys_addr_t max_address);
/* Free frames returned by pmm_alloc_pages. Ignore reserved or free frames. */
void pmm_free_pages(phys_addr_t base, size_t count);
/* Return usable frames. Include free and allocated frames. */
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
