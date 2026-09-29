#ifndef UNITAS_KERN_BOOT_H
#define UNITAS_KERN_BOOT_H

#include <kern/types.h>

#define MULTIBOOT2_BOOT_MAGIC 0x36d76289u
#define MULTIBOOT2_TAG_END 0
#define MULTIBOOT2_TAG_MEMORY_MAP 6

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct multiboot2_mmap_tag {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct multiboot2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

void kernel_main(uint32_t boot_magic, uintptr_t boot_info_address);
const struct multiboot2_mmap_tag *boot_memory_map(uintptr_t info_address,
                                                  size_t *tag_size);

#endif
