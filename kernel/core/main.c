#include <kern/boot.h>
#include <kern/acpi.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/fat32_disk.h>
#include <kern/interrupts.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/mm.h>
#include <kern/network.h>
#include <kern/panic.h>
#include <kern/procfs.h>
#include <kern/ramfs.h>
#include <kern/serial.h>
#include <kern/timer.h>
#include <kern/user.h>
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

const void *boot_acpi_rsdp(uintptr_t address, size_t *payload_size) {
    const uintptr_t address_limit = 0x100000000ULL;
    if (!payload_size || !address || address >= address_limit ||
        address_limit - address < sizeof(struct multiboot2_info)) return 0;
    const struct multiboot2_info *info = (const void *)address;
    if (info->total_size < sizeof(*info) ||
        info->total_size > 16 * 1024 * 1024 ||
        info->total_size > address_limit - address) return 0;

    uintptr_t cursor = address + sizeof(*info);
    uintptr_t end = address + info->total_size;
    const void *rsdp = 0;
    size_t rsdp_size = 0;
    while (cursor <= end && end - cursor >= sizeof(struct multiboot2_tag)) {
        const struct multiboot2_tag *tag = (const void *)cursor;
        if (tag->size < sizeof(*tag) || tag->size > end - cursor) return 0;
        if (tag->type == MULTIBOOT2_TAG_END) {
            if (tag->size != sizeof(*tag) || cursor + tag->size != end) return 0;
            *payload_size = rsdp_size;
            return rsdp;
        }
        if ((tag->type == MULTIBOOT2_TAG_ACPI_OLD ||
             tag->type == MULTIBOOT2_TAG_ACPI_NEW) &&
            tag->size > sizeof(*tag)) {
            size_t candidate_size = tag->size - sizeof(*tag);
            if (tag->type == MULTIBOOT2_TAG_ACPI_NEW || !rsdp) {
                rsdp = (const uint8_t *)tag + sizeof(*tag);
                rsdp_size = candidate_size;
            }
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
    interrupts_init();
    log_write(LOG_INFO, "UnitasOS kernel starting\n");
    if (magic != MULTIBOOT2_BOOT_MAGIC) panicf("unexpected boot protocol magic 0x%x", magic);

    size_t map_size = 0;
    const struct multiboot2_mmap_tag *map = boot_memory_map(boot_info_address, &map_size);
    if (!map) panic("Multiboot2 memory map is missing or malformed");
    const struct multiboot2_info *boot_info = (const void *)boot_info_address;
    /* Reserve boot data and enable page protections before heap use. */
    pmm_init(map, map_size, boot_info_address, boot_info->total_size);
    if (user_init() != 0) panic("failed to set up user-mode entry");
    if (paging_init() != 0) panic("failed to set up kernel page protections");
    size_t rsdp_size = 0;
    const void *rsdp = boot_acpi_rsdp(boot_info_address, &rsdp_size);
    if (acpi_init(rsdp, rsdp_size) != 0)
        log_write(LOG_WARN, "ACPI power controls are unavailable\n");
    heap_init();
    /* Start storage before root selection, but defer /dev until after the mount. */
    pic_init();
    serial_interrupts_init();
    platform_storage_init();
    int disk_root = fat32_disk_mount_root();
    if (disk_root < 0)
        log_write(LOG_WARN, "could not mount installed FAT32 root; using RAM root\n");
    else if (!disk_root)
        log_write(LOG_INFO, "using RAM FAT32 root\n");
    if (disk_root <= 0 && ramfs_init() != 0)
        panic("failed to format and mount RAM FAT32 volume");
    if (procfs_init() != 0) panic("failed to mount /proc");
    if (userland_seed() != 0) panic("failed to install user programs");
    log_write(LOG_INFO, "physical memory: %llu pages free / %llu pages tracked\n",
              (unsigned long long)pmm_free_page_count(),
              (unsigned long long)pmm_total_pages());

    /* Finish device setup before the kernel enables hardware interrupts. */
    platform_drivers_init();
    cpu_enable_interrupts();
    log_write(LOG_INFO, "interrupts enabled. Timer at 100 Hz\n");
    console_write("Welcome to GNU/Unitas\n", sizeof("Welcome to GNU/Unitas\n") - 1);
    log_write(LOG_INFO, "starting user shell\n");
    const char *shell_path = "/bin/sh";
    struct vfs_stat bash_status;
    /* Prefer Bash when the root file system contains its image. */
    if (vfs_stat("/bin/bash", &bash_status) == 0) shell_path = "/bin/bash";
    const char *shell_arguments[] = { shell_path };
    for (;;) {
        int status = user_exec(shell_path, 1, shell_arguments);
        if (status < 0) panic("failed to start /bin/sh user program");
        if (status != 0)
            log_write(LOG_WARN, "user program exited with status %d; restarting shell\n",
                      status);
    }
}
