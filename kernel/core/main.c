#include <kern/boot.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/interrupts.h>
#include <kern/io.h>
#include <kern/keyboard.h>
#include <kern/log.h>
#include <kern/mm.h>
#include <kern/panic.h>
#include <kern/ramfs.h>
#include <kern/serial.h>
#include <kern/shell.h>
#include <kern/timer.h>

const struct multiboot2_mmap_tag *boot_memory_map(uintptr_t address,
                                                  size_t *tag_size) {
    const struct multiboot2_info *info = (const void *)address;
    if (!info || info->total_size < sizeof(*info) || info->total_size > 16 * 1024 * 1024)
        return 0;
    uintptr_t cursor = address + sizeof(*info);
    const uintptr_t end = address + info->total_size;
    while (cursor + sizeof(struct multiboot2_tag) <= end) {
        const struct multiboot2_tag *tag = (const void *)cursor;
        if (tag->size < sizeof(*tag) || cursor + tag->size > end) return 0;
        if (tag->type == MULTIBOOT2_TAG_END) return 0;
        if (tag->type == MULTIBOOT2_TAG_MEMORY_MAP) {
            if (tag->size < sizeof(struct multiboot2_mmap_tag)) return 0;
            *tag_size = tag->size;
            return (const struct multiboot2_mmap_tag *)tag;
        }
        cursor = ALIGN_UP(cursor + tag->size, 8);
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
    platform_drivers_init();
    cpu_enable_interrupts();
    log_write(LOG_INFO, "interrupts enabled. Timer at 100 Hz\n");
    shell_init();

    for (;;) {
        char character;
        while (keyboard_read_char(&character)) shell_process_char(character);
        if (serial_read_char(&character)) shell_process_char(character);
        __asm__ volatile("hlt");
    }
}
