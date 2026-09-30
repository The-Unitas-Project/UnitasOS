#include <kern/block.h>
#include <kern/fat32_disk.h>
#include <kern/log.h>
#include <kern/string.h>
#include <kern/vfs.h>
#include <limits.h>

#define FAT32_SECTOR_SIZE 512u
#define FAT32_ATTR_READ_ONLY 0x01u
#define FAT32_ATTR_HIDDEN 0x02u
#define FAT32_ATTR_SYSTEM 0x04u
#define FAT32_ATTR_VOLUME 0x08u
#define FAT32_ATTR_DIRECTORY 0x10u
#define FAT32_ATTR_LONG_NAME 0x0fu
#define FAT32_EOC_MIN 0x0ffffff8u
#define FAT32_BAD_CLUSTER 0x0ffffff7u
#define FAT32_MAX_OPEN 64

struct fat32_directory_entry {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t nt_reserved;
    uint8_t creation_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modification_time;
    uint16_t modification_date;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed));

struct fat32_volume {
    struct block_device *device;
    uint32_t total_sectors;
    uint32_t reserved_sectors;
    uint32_t sectors_per_fat;
    uint32_t sectors_per_cluster;
    uint32_t first_data_sector;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t active_fat;
};

struct fat32_open_file {
    bool used;
    bool directory;
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
    struct fat32_volume *volume;
};

_Static_assert(sizeof(struct fat32_directory_entry) == 32,
               "FAT32 directory entry size");

static struct fat32_volume root_volume;
static struct fat32_open_file open_files[FAT32_MAX_OPEN];
static bool root_is_disk_backed;

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t entry_cluster(const struct fat32_directory_entry *entry) {
    return (((uint32_t)entry->cluster_high & 0x0fffu) << 16) |
           entry->cluster_low;
}

static bool volume_read_sector(const struct fat32_volume *volume,
                               uint32_t lba, uint8_t sector[FAT32_SECTOR_SIZE]) {
    return lba < volume->total_sectors &&
           block_read(volume->device, lba, 1, sector) == 0;
}

static bool cluster_sector(const struct fat32_volume *volume, uint32_t cluster,
                           uint32_t offset, uint32_t *lba) {
    if (cluster < 2 || cluster >= volume->cluster_count + 2 ||
        offset >= volume->sectors_per_cluster) return false;
    uint64_t sector = (uint64_t)volume->first_data_sector +
                      (uint64_t)(cluster - 2) * volume->sectors_per_cluster +
                      offset;
    if (sector >= volume->total_sectors || sector > UINT32_MAX) return false;
    *lba = (uint32_t)sector;
    return true;
}

static int next_cluster(const struct fat32_volume *volume, uint32_t cluster,
                        uint32_t *next) {
    uint64_t fat_sector = (uint64_t)volume->reserved_sectors +
                          (uint64_t)volume->active_fat * volume->sectors_per_fat +
                          ((uint64_t)cluster * 4) / FAT32_SECTOR_SIZE;
    if (fat_sector >= volume->total_sectors || fat_sector > UINT32_MAX)
        return -1;
    uint8_t sector[FAT32_SECTOR_SIZE];
    if (!volume_read_sector(volume, (uint32_t)fat_sector, sector)) return -1;
    uint32_t value = read_le32(sector + (cluster * 4) % FAT32_SECTOR_SIZE) &
                     0x0fffffffu;
    if (value >= FAT32_EOC_MIN) return 0;
    if (value == FAT32_BAD_CLUSTER || value < 2 ||
        value >= volume->cluster_count + 2) return -1;
    *next = value;
    return 1;
}

static bool encode_short_name(const char *component, size_t length,
                              uint8_t output[11]) {
    if (!component || !length || length > 12 ||
        (length == 1 && component[0] == '.') ||
        (length == 2 && component[0] == '.' && component[1] == '.')) {
        if (component && length == 1 && component[0] == '.') {
            memset(output, ' ', 11);
            output[0] = '.';
            return true;
        }
        if (component && length == 2 && component[0] == '.' &&
            component[1] == '.') {
            memset(output, ' ', 11);
            output[0] = '.';
            output[1] = '.';
            return true;
        }
        return false;
    }

    memset(output, ' ', 11);
    size_t base_length = 0;
    size_t extension_length = 0;
    bool in_extension = false;
    for (size_t index = 0; index < length; ++index) {
        unsigned char value = (unsigned char)component[index];
        if (value == '.') {
            if (in_extension || !base_length) return false;
            in_extension = true;
            continue;
        }
        if (value < 0x21 || value >= 0x7f || value == '"' || value == '*' ||
            value == '/' || value == ':' || value == '<' || value == '>' ||
            value == '?' || value == '\\' || value == '|') return false;
        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        if (in_extension) {
            if (extension_length == 3) return false;
            output[8 + extension_length++] = value;
        } else {
            if (base_length == 8) return false;
            output[base_length++] = value;
        }
    }
    return base_length != 0;
}

static int find_entry(const struct fat32_volume *volume, uint32_t directory,
                      const uint8_t name[11],
                      struct fat32_directory_entry *result) {
    uint32_t cluster = directory;
    for (uint32_t visited = 0; visited < volume->cluster_count; ++visited) {
        for (uint32_t sector_index = 0;
             sector_index < volume->sectors_per_cluster; ++sector_index) {
            uint32_t lba;
            uint8_t sector[FAT32_SECTOR_SIZE];
            if (!cluster_sector(volume, cluster, sector_index, &lba) ||
                !volume_read_sector(volume, lba, sector)) return -1;
            const struct fat32_directory_entry *entries = (const void *)sector;
            for (size_t index = 0;
                 index < FAT32_SECTOR_SIZE / sizeof(*entries); ++index) {
                const struct fat32_directory_entry *entry = &entries[index];
                if (entry->name[0] == 0) return 0;
                if (entry->name[0] == 0xe5 ||
                    entry->attributes == FAT32_ATTR_LONG_NAME ||
                    (entry->attributes & FAT32_ATTR_VOLUME)) continue;
                if (memcmp(entry->name, name, 11) == 0) {
                    *result = *entry;
                    return 1;
                }
            }
        }
        uint32_t following;
        int state = next_cluster(volume, cluster, &following);
        if (state <= 0) return state;
        cluster = following;
    }
    return -1;
}

static bool resolve_path(struct fat32_volume *volume, const char *path,
                         struct fat32_directory_entry *result) {
    if (!path || path[0] != '/') return false;
    size_t length = strlen(path);
    if (length >= 256) return false;
    while (length > 1 && path[length - 1] == '/') --length;
    memset(result, 0, sizeof(*result));
    result->attributes = FAT32_ATTR_DIRECTORY;
    result->cluster_low = (uint16_t)volume->root_cluster;
    result->cluster_high = (uint16_t)(volume->root_cluster >> 16);
    if (length == 1) return true;

    uint32_t directory = volume->root_cluster;
    size_t cursor = 1;
    while (cursor < length) {
        size_t end = cursor;
        while (end < length && path[end] != '/') ++end;
        if (end == cursor) return false;
        uint8_t short_name[11];
        if (!encode_short_name(path + cursor, end - cursor, short_name))
            return false;

        struct fat32_directory_entry entry;
        if (end - cursor == 1 && path[cursor] == '.') {
            memset(result, 0, sizeof(*result));
            result->attributes = FAT32_ATTR_DIRECTORY;
            result->cluster_low = (uint16_t)directory;
            result->cluster_high = (uint16_t)(directory >> 16);
        } else if (end - cursor == 2 && path[cursor] == '.' &&
            path[cursor + 1] == '.' && directory == volume->root_cluster) {
            result->attributes = FAT32_ATTR_DIRECTORY;
            result->cluster_low = (uint16_t)volume->root_cluster;
            result->cluster_high = (uint16_t)(volume->root_cluster >> 16);
        } else {
            int found = find_entry(volume, directory, short_name, &entry);
            if (found != 1) return false;
            *result = entry;
        }
        if (end == length) return true;
        if (!(result->attributes & FAT32_ATTR_DIRECTORY)) return false;
        directory = entry_cluster(result);
        if (directory < 2 || directory >= volume->cluster_count + 2)
            return false;
        cursor = end + 1;
    }
    return false;
}

static void fill_stat(const struct fat32_directory_entry *entry,
                      struct vfs_stat *result) {
    memset(result, 0, sizeof(*result));
    bool directory = (entry->attributes & FAT32_ATTR_DIRECTORY) != 0;
    result->inode = entry_cluster(entry);
    result->size = entry->size;
    result->blocks = ((uint64_t)entry->size + FAT32_SECTOR_SIZE - 1) /
                     FAT32_SECTOR_SIZE;
    result->block_size = FAT32_SECTOR_SIZE;
    result->links = directory ? 2 : 1;
    result->mode = directory ? (0040000 | 0555) : (0100000 | 0444);
}

static int disk_fat32_open(const char *path, uint32_t flags, void *data,
                           void **node) {
    struct fat32_volume *volume = data;
    if (!volume || volume != &root_volume || !node || flags) return -1;
    struct fat32_directory_entry entry;
    if (!resolve_path(volume, path, &entry)) return -1;
    for (size_t index = 0; index < FAT32_MAX_OPEN; ++index) {
        if (open_files[index].used) continue;
        open_files[index] = (struct fat32_open_file) {
            .used = true,
            .directory = (entry.attributes & FAT32_ATTR_DIRECTORY) != 0,
            .attributes = entry.attributes,
            .first_cluster = entry_cluster(&entry),
            .size = entry.size,
            .volume = volume
        };
        *node = &open_files[index];
        return 0;
    }
    return -1;
}

static int disk_fat32_read(void *node, uint64_t offset, void *buffer,
                           size_t length) {
    struct fat32_open_file *file = node;
    if (!file || !file->used || file->directory || (!buffer && length) ||
        length > INT_MAX) return -1;
    if (offset >= file->size || !length) return 0;
    if ((uint64_t)length > file->size - offset)
        length = (size_t)(file->size - offset);

    struct fat32_volume *volume = file->volume;
    uint64_t cluster_bytes =
        (uint64_t)volume->sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint64_t within_cluster = offset % cluster_bytes;
    uint64_t cluster_number = offset / cluster_bytes;
    if (cluster_number >= volume->cluster_count) return -1;
    uint32_t cluster = file->first_cluster;
    if (cluster < 2 || cluster >= volume->cluster_count + 2) return -1;
    for (uint64_t visited = 0; visited < cluster_number; ++visited) {
        uint32_t following;
        if (next_cluster(volume, cluster, &following) != 1) return -1;
        cluster = following;
    }

    size_t copied = 0;
    uint64_t visited_clusters = cluster_number;
    while (copied < length) {
        uint32_t sector_index = (uint32_t)(within_cluster / FAT32_SECTOR_SIZE);
        size_t sector_offset = (size_t)(within_cluster % FAT32_SECTOR_SIZE);
        uint32_t lba;
        uint8_t sector[FAT32_SECTOR_SIZE];
        if (!cluster_sector(volume, cluster, sector_index, &lba) ||
            !volume_read_sector(volume, lba, sector))
            return copied ? (int)copied : -1;
        size_t amount = FAT32_SECTOR_SIZE - sector_offset;
        if (amount > length - copied) amount = length - copied;
        memcpy((uint8_t *)buffer + copied, sector + sector_offset, amount);
        copied += amount;
        within_cluster += amount;
        if (within_cluster == cluster_bytes && copied < length) {
            uint32_t following;
            if (++visited_clusters >= volume->cluster_count ||
                next_cluster(volume, cluster, &following) != 1)
                return copied ? (int)copied : -1;
            cluster = following;
            within_cluster = 0;
        }
    }
    return (int)copied;
}

static void disk_fat32_close(void *node) {
    struct fat32_open_file *file = node;
    if (file) *file = (struct fat32_open_file){0};
}

static int disk_fat32_stat_path(const char *path, void *data,
                                struct vfs_stat *result) {
    struct fat32_volume *volume = data;
    if (!volume || volume != &root_volume || !result) return -1;
    struct fat32_directory_entry entry;
    if (!resolve_path(volume, path, &entry)) return -1;
    fill_stat(&entry, result);
    return 0;
}

static int disk_fat32_stat_node(void *node, struct vfs_stat *result) {
    struct fat32_open_file *file = node;
    if (!file || !file->used || !result) return -1;
    struct fat32_directory_entry entry = {
        .attributes = file->attributes,
        .cluster_high = (uint16_t)(file->first_cluster >> 16),
        .cluster_low = (uint16_t)file->first_cluster,
        .size = file->size
    };
    fill_stat(&entry, result);
    return 0;
}

static void decode_short_name(const uint8_t name[11], char output[256]) {
    size_t cursor = 0;
    size_t base_length = 8;
    while (base_length && name[base_length - 1] == ' ') --base_length;
    for (size_t index = 0; index < base_length; ++index) {
        char character = (char)name[index];
        if (character >= 'A' && character <= 'Z')
            character = (char)(character - 'A' + 'a');
        output[cursor++] = character;
    }
    size_t extension_length = 3;
    while (extension_length && name[8 + extension_length - 1] == ' ')
        --extension_length;
    if (extension_length) {
        output[cursor++] = '.';
        for (size_t index = 0; index < extension_length; ++index) {
            char character = (char)name[8 + index];
            if (character >= 'A' && character <= 'Z')
                character = (char)(character - 'A' + 'a');
            output[cursor++] = character;
        }
    }
    output[cursor] = 0;
}

static int disk_fat32_readdir(const char *path, void *data, uint64_t wanted,
                              struct vfs_dirent *output) {
    struct fat32_volume *volume = data;
    if (!volume || volume != &root_volume || !output) return -1;
    struct fat32_directory_entry directory_entry;
    if (!resolve_path(volume, path, &directory_entry) ||
        !(directory_entry.attributes & FAT32_ATTR_DIRECTORY)) return -1;
    uint32_t cluster = entry_cluster(&directory_entry);
    uint64_t visible = 0;
    for (uint32_t visited = 0; visited < volume->cluster_count; ++visited) {
        for (uint32_t sector_index = 0;
             sector_index < volume->sectors_per_cluster; ++sector_index) {
            uint32_t lba;
            uint8_t sector[FAT32_SECTOR_SIZE];
            if (!cluster_sector(volume, cluster, sector_index, &lba) ||
                !volume_read_sector(volume, lba, sector)) return -1;
            const struct fat32_directory_entry *entries = (const void *)sector;
            for (size_t index = 0;
                 index < FAT32_SECTOR_SIZE / sizeof(*entries); ++index) {
                const struct fat32_directory_entry *entry = &entries[index];
                if (!entry->name[0]) return 0;
                if (entry->name[0] == 0xe5 || entry->name[0] == '.' ||
                    entry->attributes == FAT32_ATTR_LONG_NAME ||
                    (entry->attributes & FAT32_ATTR_VOLUME) ||
                    (entry->attributes & (FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM)))
                    continue;
                if (visible++ != wanted) continue;
                decode_short_name(entry->name, output->name);
                output->type =
                    (entry->attributes & FAT32_ATTR_DIRECTORY) ? 1 : 0;
                output->size = entry->size;
                return 1;
            }
        }
        uint32_t following;
        int state = next_cluster(volume, cluster, &following);
        if (state < 0) return -1;
        if (!state) return 0;
        cluster = following;
    }
    return -1;
}

static const struct filesystem disk_fat32 = {
    .name = "fat32-disk-ro",
    .open = disk_fat32_open,
    .read = disk_fat32_read,
    .close = disk_fat32_close,
    .stat_path = disk_fat32_stat_path,
    .stat_node = disk_fat32_stat_node,
    .readdir = disk_fat32_readdir
};

static bool parse_volume(struct block_device *device,
                         struct fat32_volume *volume) {
    uint8_t boot[FAT32_SECTOR_SIZE];
    if (block_read(device, 0, 1, boot) != 0 ||
        boot[510] != 0x55 || boot[511] != 0xaa ||
        read_le16(boot + 11) != FAT32_SECTOR_SIZE ||
        boot[13] == 0 || (boot[13] & (boot[13] - 1)) != 0 ||
        boot[13] > 128 || !read_le16(boot + 14) ||
        (boot[16] != 1 && boot[16] != 2) || read_le16(boot + 17) != 0 ||
        read_le16(boot + 22) != 0 || read_le16(boot + 42) != 0 ||
        memcmp(boot + 71, "UNITASOS   ", 11) != 0) return false;

    uint32_t total_sectors = read_le32(boot + 32);
    uint32_t sectors_per_fat = read_le32(boot + 36);
    uint32_t reserved_sectors = read_le16(boot + 14);
    uint64_t fat_area = (uint64_t)boot[16] * sectors_per_fat;
    uint64_t first_data = (uint64_t)reserved_sectors + fat_area;
    if (!total_sectors || total_sectors > device->sector_count ||
        !sectors_per_fat || first_data >= total_sectors ||
        first_data > UINT32_MAX) return false;

    uint32_t cluster_count =
        (uint32_t)((total_sectors - first_data) / boot[13]);
    uint32_t root_cluster = read_le32(boot + 44) & 0x0fffffffu;
    uint64_t fat_capacity = (uint64_t)sectors_per_fat * FAT32_SECTOR_SIZE / 4;
    uint16_t flags = read_le16(boot + 40);
    uint32_t active_fat = (flags & 0x80) ? flags & 0x0f : 0;
    if (cluster_count < 65525 || cluster_count > UINT32_MAX - 2 ||
        root_cluster < 2 || root_cluster >= cluster_count + 2 ||
        fat_capacity < (uint64_t)cluster_count + 2 || active_fat >= boot[16])
        return false;

    *volume = (struct fat32_volume) {
        .device = device,
        .total_sectors = total_sectors,
        .reserved_sectors = reserved_sectors,
        .sectors_per_fat = sectors_per_fat,
        .sectors_per_cluster = boot[13],
        .first_data_sector = (uint32_t)first_data,
        .cluster_count = cluster_count,
        .root_cluster = root_cluster,
        .active_fat = active_fat
    };
    return true;
}

static bool installed_programs_exist(struct fat32_volume *volume) {
    static const char *const paths[] = {
        "/bin/hello.elf", "/bin/reboot", "/bin/poweroff", "/bin/shutdown",
        "/bin/sh"
    };
    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        struct fat32_directory_entry entry;
        if (!resolve_path(volume, paths[index], &entry) ||
            (entry.attributes & FAT32_ATTR_DIRECTORY)) return false;
    }
    return true;
}

int fat32_disk_mount_root(void) {
    root_is_disk_backed = false;
    memset(&root_volume, 0, sizeof(root_volume));
    memset(open_files, 0, sizeof(open_files));
    for (size_t index = 0; index < block_device_count(); ++index) {
        struct block_device *device = block_device_at(index);
        struct fat32_volume candidate;
        if (!device || !parse_volume(device, &candidate) ||
            !installed_programs_exist(&candidate)) continue;
        root_volume = candidate;
        if (vfs_register(&disk_fat32) != 0 ||
            vfs_mount("/", &disk_fat32, &root_volume) != 0) {
            memset(&root_volume, 0, sizeof(root_volume));
            return -1;
        }
        root_is_disk_backed = true;
        log_write(LOG_INFO, "mounted read-only FAT32 root from /dev/%s\n",
                  device->name);
        return 1;
    }
    log_write(LOG_INFO, "no installed FAT32 root found\n");
    return 0;
}

bool fat32_disk_root_active(void) {
    return root_is_disk_backed;
}
