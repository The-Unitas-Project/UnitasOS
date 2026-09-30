#include <kern/block.h>
#include <kern/log.h>
#include <kern/partition.h>
#include <kern/string.h>
#include <stdbool.h>

/* Partition devices translate bounded child LBAs into the parent disk. */
#define GPT_HEADER_SIZE_MIN 92u
#define GPT_ENTRY_SIZE 128u
#define GPT_ENTRY_LIMIT 128u
#define GPT_SIGNATURE "EFI PART"

struct partition_view {
    struct block_device block;
    struct block_device *parent;
    uint64_t first_lba;
};

static struct partition_view views[BLOCK_MAX_DEVICES];
static size_t view_count;
static bool scan_complete;

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data) {
    return (uint64_t)read_le32(data) | ((uint64_t)read_le32(data + 4) << 32);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t crc32(const uint8_t *data, size_t length) {
    return crc32_update(0xffffffffu, data, length) ^ 0xffffffffu;
}

static bool guid_is_zero(const uint8_t *guid) {
    for (size_t i = 0; i < 16; ++i)
        if (guid[i]) return false;
    return true;
}

static bool ranges_overlap(const struct block_device *parent,
                           uint64_t first_lba, uint64_t sector_count) {
    uint64_t last_lba = first_lba + sector_count - 1;
    for (size_t i = 0; i < view_count; ++i) {
        const struct partition_view *view = &views[i];
        if (view->parent != parent) continue;
        uint64_t other_last = view->first_lba + view->block.sector_count - 1;
        if (first_lba <= other_last && view->first_lba <= last_lba) return true;
    }
    return false;
}

static int partition_read(struct block_device *block, uint64_t lba,
                          uint32_t count, void *buffer) {
    struct partition_view *view = block->private_data;
    if (!view || lba >= block->sector_count || count > block->sector_count - lba)
        return -1;
    return block_read(view->parent, view->first_lba + lba, count, buffer);
}

static int partition_write(struct block_device *block, uint64_t lba,
                           uint32_t count, const void *buffer) {
    struct partition_view *view = block->private_data;
    if (!view || lba >= block->sector_count || count > block->sector_count - lba)
        return -1;
    return block_write(view->parent, view->first_lba + lba, count, buffer);
}

static int partition_flush(struct block_device *block) {
    struct partition_view *view = block->private_data;
    return view ? block_flush(view->parent) : -1;
}

static bool make_partition_name(char output[BLOCK_NAME_MAX],
                                const char *disk_name, unsigned number) {
    size_t length = 0;
    while (length < BLOCK_NAME_MAX && disk_name[length]) ++length;
    if (!length || length >= BLOCK_NAME_MAX) return false;
    bool needs_separator = disk_name[length - 1] >= '0' &&
                           disk_name[length - 1] <= '9';
    char digits[10];
    size_t digit_count = 0;
    do {
        digits[digit_count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && digit_count < sizeof(digits));
    if (length + (needs_separator ? 1 : 0) + digit_count >= BLOCK_NAME_MAX)
        return false;
    memcpy(output, disk_name, length);
    if (needs_separator) output[length++] = 'p';
    while (digit_count) output[length++] = digits[--digit_count];
    output[length] = 0;
    return true;
}

static bool add_partition(struct block_device *parent, unsigned number,
                          uint64_t first_lba, uint64_t last_lba) {
    if (first_lba > last_lba || last_lba >= parent->sector_count ||
        view_count == BLOCK_MAX_DEVICES) return false;
    uint64_t sector_count = last_lba - first_lba + 1;
    if (ranges_overlap(parent, first_lba, sector_count)) {
        log_write(LOG_WARN, "skip overlapping partition %s index %u\n",
                  parent->name, number);
        return false;
    }

    struct partition_view *view = &views[view_count];
    if (!make_partition_name(view->block.name, parent->name, number)) return false;
    view->parent = parent;
    view->first_lba = first_lba;
    view->block.sector_count = sector_count;
    view->block.private_data = view;
    view->block.read = partition_read;
    view->block.write = parent->write ? partition_write : 0;
    view->block.flush = parent->flush ? partition_flush : 0;
    if (block_register(&view->block) != 0) return false;
    ++view_count;
    log_write(LOG_INFO, "found partition %s (%llu sectors)\n",
              view->block.name, (unsigned long long)sector_count);
    return true;
}

struct gpt_copy {
    uint8_t disk_guid[16];
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc;
};

static int read_gpt_copy(struct block_device *device, uint64_t header_lba,
                         uint64_t expected_backup_lba,
                         struct gpt_copy *copy) {
    uint8_t header[BLOCK_SECTOR_SIZE];
    if (block_read(device, header_lba, 1, header) != 0) return -1;
    if (memcmp(header, GPT_SIGNATURE, 8) != 0) return 0;

    uint32_t header_size = read_le32(header + 12);
    uint32_t header_crc = read_le32(header + 16);
    uint64_t current_lba = read_le64(header + 24);
    uint64_t backup_lba = read_le64(header + 32);
    uint64_t first_usable = read_le64(header + 40);
    uint64_t last_usable = read_le64(header + 48);
    uint64_t entries_lba = read_le64(header + 72);
    uint32_t entry_count = read_le32(header + 80);
    uint32_t entry_size = read_le32(header + 84);
    uint32_t entries_crc = read_le32(header + 88);
    uint64_t disk_last_lba = device->sector_count - 1;

    if (read_le32(header + 8) != 0x00010000 ||
        read_le32(header + 20) != 0 || header_size < GPT_HEADER_SIZE_MIN ||
        header_size > BLOCK_SECTOR_SIZE || current_lba != header_lba ||
        backup_lba != expected_backup_lba || backup_lba == current_lba ||
        first_usable < 2 || first_usable > last_usable ||
        last_usable >= disk_last_lba || entries_lba < 2 ||
        entries_lba >= device->sector_count || !entry_count ||
        entry_count > GPT_ENTRY_LIMIT || entry_size != GPT_ENTRY_SIZE ||
        guid_is_zero(header + 56)) {
        log_write(LOG_WARN, "invalid GPT header on %s at LBA %llu\n",
                  device->name, (unsigned long long)header_lba);
        return -1;
    }

    /* The header CRC covers its declared size with the CRC field cleared. */
    uint8_t header_copy[BLOCK_SECTOR_SIZE];
    memcpy(header_copy, header, sizeof(header_copy));
    memset(header_copy + 16, 0, 4);
    if (crc32(header_copy, header_size) != header_crc) {
        log_write(LOG_WARN, "GPT header CRC failed on %s at LBA %llu\n",
                  device->name, (unsigned long long)header_lba);
        return -1;
    }

    uint64_t entry_bytes = (uint64_t)entry_count * entry_size;
    uint64_t table_sectors = (entry_bytes + BLOCK_SECTOR_SIZE - 1) /
                             BLOCK_SECTOR_SIZE;
    if (table_sectors > device->sector_count - entries_lba ||
        (current_lba == 1 &&
         (entries_lba <= current_lba || entries_lba >= first_usable ||
          table_sectors > first_usable - entries_lba)) ||
        (current_lba != 1 &&
         (entries_lba <= last_usable || entries_lba >= current_lba ||
          table_sectors > current_lba - entries_lba))) {
        log_write(LOG_WARN, "GPT entry array is out of range on %s\n", device->name);
        return -1;
    }

    uint32_t crc = 0xffffffffu;
    uint64_t bytes_left = entry_bytes;
    uint8_t sector[BLOCK_SECTOR_SIZE];
    for (uint64_t index = 0; index < table_sectors; ++index) {
        if (block_read(device, entries_lba + index, 1, sector) != 0) return -1;
        size_t amount = bytes_left > sizeof(sector) ? sizeof(sector) : (size_t)bytes_left;
        crc = crc32_update(crc, sector, amount);
        bytes_left -= amount;
    }
    if ((crc ^ 0xffffffffu) != entries_crc) {
        log_write(LOG_WARN, "GPT entry-array CRC failed on %s at LBA %llu\n",
                  device->name, (unsigned long long)header_lba);
        return -1;
    }

    memcpy(copy->disk_guid, header + 56, sizeof(copy->disk_guid));
    copy->first_usable = first_usable;
    copy->last_usable = last_usable;
    copy->entries_lba = entries_lba;
    copy->entry_count = entry_count;
    copy->entry_size = entry_size;
    copy->entries_crc = entries_crc;
    return 1;
}

static bool protective_mbr_present(struct block_device *device) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (block_read(device, 0, 1, sector) != 0 ||
        sector[510] != 0x55 || sector[511] != 0xaa) return false;
    for (size_t index = 0; index < 4; ++index)
        if (sector[446 + index * 16 + 4] == 0xee) return true;
    return false;
}

static bool gpt_copies_match(const struct gpt_copy *primary,
                             const struct gpt_copy *backup) {
    return memcmp(primary->disk_guid, backup->disk_guid, 16) == 0 &&
           primary->first_usable == backup->first_usable &&
           primary->last_usable == backup->last_usable &&
           primary->entry_count == backup->entry_count &&
           primary->entry_size == backup->entry_size &&
           primary->entries_crc == backup->entries_crc;
}

static int scan_gpt(struct block_device *device, unsigned *added_out) {
    *added_out = 0;
    if (device->sector_count < 3) return 0;

    /* Validate both copies. Use one valid copy when the other is damaged. */
    struct gpt_copy primary, backup;
    int primary_result = read_gpt_copy(device, 1, device->sector_count - 1,
                                       &primary);
    if (primary_result == 0 && !protective_mbr_present(device)) return 0;
    int backup_result = read_gpt_copy(device, device->sector_count - 1, 1,
                                      &backup);
    if (primary_result == 1 && backup_result == 1 &&
        !gpt_copies_match(&primary, &backup)) {
        log_write(LOG_WARN, "GPT copies disagree on %s\n", device->name);
        return -1;
    }
    const struct gpt_copy *selected;
    if (primary_result == 1) {
        selected = &primary;
        if (backup_result != 1)
            log_write(LOG_WARN, "using primary GPT on %s; backup is invalid\n",
                      device->name);
    } else if (backup_result == 1) {
        selected = &backup;
        log_write(LOG_WARN, "using backup GPT on %s\n", device->name);
    } else if (primary_result == 0 && backup_result == 0) {
        return 0;
    } else {
        return -1;
    }

    uint64_t entry_bytes = (uint64_t)selected->entry_count * selected->entry_size;
    uint64_t table_sectors = (entry_bytes + BLOCK_SECTOR_SIZE - 1) /
                             BLOCK_SECTOR_SIZE;
    uint8_t sector[BLOCK_SECTOR_SIZE];
    unsigned added = 0;
    for (uint64_t index = 0; index < table_sectors; ++index) {
        if (block_read(device, selected->entries_lba + index, 1, sector) != 0)
            return -1;
        for (unsigned slot = 0; slot < BLOCK_SECTOR_SIZE / GPT_ENTRY_SIZE; ++slot) {
            uint32_t number = (uint32_t)(index *
                (BLOCK_SECTOR_SIZE / GPT_ENTRY_SIZE) + slot + 1);
            const uint8_t *entry = sector + slot * GPT_ENTRY_SIZE;
            if (number > selected->entry_count || guid_is_zero(entry)) continue;
            uint64_t first_lba = read_le64(entry + 32);
            uint64_t last_lba = read_le64(entry + 40);
            if (first_lba < selected->first_usable ||
                last_lba > selected->last_usable) {
                log_write(LOG_WARN, "skip out-of-range GPT partition %s index %u\n",
                          device->name, number);
                continue;
            }
            if (add_partition(device, number, first_lba, last_lba)) ++added;
        }
    }
    *added_out = added;
    return 1;
}

static bool extended_partition(uint8_t type) {
    return type == 0x05 || type == 0x0f || type == 0x85;
}

static unsigned scan_extended(struct block_device *device, uint64_t base,
                              uint64_t count, unsigned *partition_number) {
    /* Extended Boot Record (EBR) links use offsets from the extended partition and may cycle. */
    uint64_t visited[128];
    size_t visited_count = 0;
    uint64_t current = base;
    uint64_t end = base + count - 1;
    unsigned added = 0;
    while (visited_count < sizeof(visited) / sizeof(visited[0])) {
        if (current < base || current > end) break;
        bool seen = false;
        for (size_t i = 0; i < visited_count; ++i)
            if (visited[i] == current) seen = true;
        if (seen) {
            log_write(LOG_WARN, "EBR loop on %s at LBA %llu\n", device->name,
                      (unsigned long long)current);
            break;
        }
        visited[visited_count++] = current;

        uint8_t sector[BLOCK_SECTOR_SIZE];
        if (block_read(device, current, 1, sector) != 0 ||
            sector[510] != 0x55 || sector[511] != 0xaa) {
            log_write(LOG_WARN, "invalid EBR on %s at LBA %llu\n", device->name,
                      (unsigned long long)current);
            break;
        }

        const uint8_t *logical = sector + 446;
        uint8_t logical_type = logical[4];
        uint64_t relative = read_le32(logical + 8);
        uint64_t logical_count = read_le32(logical + 12);
        if (logical_type && !extended_partition(logical_type) && logical_count &&
            relative <= end - current) {
            uint64_t first = current + relative;
            if (first <= end && logical_count <= end - first + 1) {
                if (add_partition(device, *partition_number, first,
                                  first + logical_count - 1)) ++added;
                ++*partition_number;
            } else {
                log_write(LOG_WARN, "skip out-of-range logical partition on %s\n",
                          device->name);
            }
        }

        const uint8_t *link = logical + 16;
        uint64_t next_relative = read_le32(link + 8);
        if (!extended_partition(link[4]) || !next_relative ||
            next_relative > end - base) break;
        current = base + next_relative;
    }
    if (visited_count == sizeof(visited) / sizeof(visited[0]))
        log_write(LOG_WARN, "EBR chain limit reached on %s\n", device->name);
    return added;
}

static unsigned scan_mbr(struct block_device *device) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (block_read(device, 0, 1, sector) != 0 ||
        sector[510] != 0x55 || sector[511] != 0xaa) return 0;

    unsigned added = 0;
    unsigned logical_number = 5;
    for (unsigned index = 0; index < 4; ++index) {
        const uint8_t *entry = sector + 446 + index * 16;
        uint8_t type = entry[4];
        uint64_t first_lba = read_le32(entry + 8);
        uint64_t sector_count = read_le32(entry + 12);
        if (!type || type == 0xee || !sector_count) continue;
        if (extended_partition(type)) {
            if (first_lba && first_lba < device->sector_count &&
                sector_count <= device->sector_count - first_lba)
                added += scan_extended(device, first_lba, sector_count,
                                       &logical_number);
            else
                log_write(LOG_WARN, "skip out-of-range extended partition on %s\n",
                          device->name);
            continue;
        }
        if (!first_lba || first_lba >= device->sector_count ||
            sector_count > device->sector_count - first_lba) {
            log_write(LOG_WARN, "skip out-of-range MBR partition %s index %u\n",
                      device->name, index + 1);
            continue;
        }
        if (add_partition(device, index + 1, first_lba,
                          first_lba + sector_count - 1)) ++added;
    }
    return added;
}

int partition_init(void) {
    if (scan_complete) return 0;
    scan_complete = true;
    size_t disk_count = block_device_count();
    unsigned found = 0;
    for (size_t index = 0; index < disk_count; ++index) {
        struct block_device *device = block_device_at(index);
        int result = partition_scan_device(device);
        if (result > 0) found += (unsigned)result;
    }
    log_write(LOG_INFO, "partition scan registered %u partition(s)\n", found);
    return 0;
}

int partition_scan_device(struct block_device *device) {
    if (!device) return -1;
    bool registered = false;
    for (size_t index = 0; index < block_device_count(); ++index) {
        if (block_device_at(index) == device) {
            registered = true;
            break;
        }
    }
    if (!registered) return -1;
    unsigned gpt_partitions = 0;
    int gpt_result = scan_gpt(device, &gpt_partitions);
    if (gpt_result > 0) return (int)gpt_partitions;
    if (gpt_result == 0) return (int)scan_mbr(device);
    return -1;
}
