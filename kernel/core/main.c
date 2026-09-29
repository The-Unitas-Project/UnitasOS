#include <kern/boot.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/interrupts.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/mm.h>
#include <kern/panic.h>
#include <kern/ramfs.h>
#include <kern/serial.h>
#include <kern/shell.h>
#include <kern/timer.h>
#include <kern/vfs.h>

const struct multiboot2_mmap_tag *boot_memory_map(uintptr_t address,
                                                  size_t *tag_size) {
    const uintptr_t boot_address_limit = 0x100000000ULL;
    if (!tag_size || !address || address >= boot_address_limit ||
        boot_address_limit - address < sizeof(struct multiboot2_info))
        return 0;
    const struct multiboot2_info *info = (const void *)address;
    if (info->total_size < sizeof(*info) ||
        info->total_size > 16 * 1024 * 1024 ||
        info->total_size > boot_address_limit - address)
        return 0;
    uintptr_t cursor = address + sizeof(*info);
    const uintptr_t end = address + info->total_size;
    const struct multiboot2_mmap_tag *memory_map = 0;
    size_t memory_map_size = 0;
    while (cursor <= end && end - cursor >= sizeof(struct multiboot2_tag)) {
        const struct multiboot2_tag *tag = (const void *)cursor;
        if (tag->size < sizeof(*tag) || tag->size > end - cursor) return 0;
        if (tag->type == MULTIBOOT2_TAG_END) {
            if (tag->size != sizeof(*tag) || cursor + tag->size != end ||
                !memory_map)
                return 0;
            *tag_size = memory_map_size;
            return memory_map;
        }
        if (tag->type == MULTIBOOT2_TAG_MEMORY_MAP) {
            if (memory_map || tag->size < sizeof(struct multiboot2_mmap_tag)) return 0;
            const struct multiboot2_mmap_tag *candidate = (const void *)tag;
            size_t entries_size = tag->size - sizeof(*candidate);
            if (candidate->entry_version != 0 ||
                candidate->entry_size < sizeof(struct multiboot2_mmap_entry) ||
                entries_size < candidate->entry_size ||
                entries_size % candidate->entry_size != 0)
                return 0;
            memory_map = candidate;
            memory_map_size = tag->size;
        }
        uintptr_t next = ALIGN_UP(cursor + tag->size, 8);
        if (next <= cursor || next > end) return 0;
        cursor = next;
    }
    return 0;
}

void kernel_main(uint32_t magic, uintptr_t boot_info_address) {
    console_init();
    log_init();
    log_write(LOG_INFO, "UnitasOS kernel starting\n");
    if (magic != MULTIBOOT2_BOOT_MAGIC) panicf("unexpected boot protocol magic 0x%x", magic);

    size_t map_size = 0;
    const struct multiboot2_mmap_tag *map = boot_memory_map(boot_info_address, &map_size);
    if (!map) panic("Multiboot2 memory map is missing or malformed");
    const struct multiboot2_info *boot_info = (const void *)boot_info_address;
    pmm_init(map, map_size, boot_info_address, boot_info->total_size);
    heap_init();
    if (ramfs_init() != 0) panic("failed to format and mount RAM FAT32 volume");
    log_write(LOG_INFO, "physical memory: %llu pages free / %llu pages tracked\n",
              (unsigned long long)pmm_free_page_count(),
              (unsigned long long)pmm_total_pages());

    interrupts_init();
    pic_init();
    serial_interrupts_init();
    platform_drivers_init();
    cpu_enable_interrupts();
    log_write(LOG_INFO, "interrupts enabled. Timer at 100 Hz\n");
    shell_init();

    int console_input;
    if (vfs_open("/dev/console", 0, &console_input) < 0)
        panic("failed to open console input device");

    for (;;) {
        char character;
        int amount;
        while ((amount = vfs_read(console_input, &character, 1)) > 0)
            shell_process_char(character);
        if (amount < 0) panic("console input read failed");
        __asm__ volatile("hlt");
    }
}
