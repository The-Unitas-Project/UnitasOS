#include <kern/ramfs.h>
#include <kern/string.h>
#include <kern/vfs.h>
#include <limits.h>

#define FAT_SECTOR_SIZE 512u
#define RAM_VOLUME_BYTES (64u * 1024u * 1024u)
#define RAM_TOTAL_SECTORS (RAM_VOLUME_BYTES / FAT_SECTOR_SIZE)
#define FAT_RESERVED_SECTORS 32u
#define FAT_COUNT 2u
#define FAT_EOC 0x0fffffffu
#define FAT_EOC_MIN 0x0ffffff8u
#define FAT_ATTR_DIRECTORY 0x10u
#define FAT_ATTR_VOLUME 0x08u

struct fat_directory_entry {
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

#define FAT_MAX_OPEN_FILES 64
struct fat_open_file {
    struct fat_directory_entry *entry;
    bool used;
};

_Static_assert(sizeof(struct fat_directory_entry) == 32, "FAT directory entry size");

/* 64 MiB with 512-byte clusters meets FAT32's minimum cluster count. */
static uint8_t ram_volume[RAM_VOLUME_BYTES] __attribute__((aligned(4096)));
static uint32_t fat_sectors;
static uint32_t data_start_sector;
static uint32_t cluster_count;
static uint32_t next_free_cluster = 3;
static struct fat_open_file open_files[FAT_MAX_OPEN_FILES];

static uint8_t *sector(uint32_t number) {
    return ram_volume + (size_t)number * FAT_SECTOR_SIZE;
}

static uint32_t load_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint8_t *cluster_data(uint32_t cluster) {
    return sector(data_start_sector + cluster - 2);
}

static uint32_t fat_get(uint32_t cluster) {
    uint32_t sector_number = FAT_RESERVED_SECTORS + (cluster * 4) / FAT_SECTOR_SIZE;
    uint32_t offset = (cluster * 4) % FAT_SECTOR_SIZE;
    return load_le32(sector(sector_number) + offset) & 0x0fffffff;
}

static void fat_set(uint32_t cluster, uint32_t value) {
    uint32_t sector_number = FAT_RESERVED_SECTORS + (cluster * 4) / FAT_SECTOR_SIZE;
    uint32_t offset = (cluster * 4) % FAT_SECTOR_SIZE;
    for (uint32_t copy = 0; copy < FAT_COUNT; ++copy)
        store_le32(sector(sector_number + copy * fat_sectors) + offset, value);
}

static uint32_t entry_cluster(const struct fat_directory_entry *entry) {
    return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

static void set_entry_cluster(struct fat_directory_entry *entry, uint32_t cluster) {
    entry->cluster_high = (uint16_t)(cluster >> 16);
    entry->cluster_low = (uint16_t)cluster;
}

static uint32_t allocate_cluster(void) {
    uint32_t first = next_free_cluster;
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
        uint32_t candidate = 2 + ((first - 2 + visited) % cluster_count);
        if (fat_get(candidate) != 0) continue;
        fat_set(candidate, FAT_EOC);
        memset(cluster_data(candidate), 0, FAT_SECTOR_SIZE);
        next_free_cluster = candidate + 1;
        if (next_free_cluster >= cluster_count + 2) next_free_cluster = 2;
        return candidate;
    }
    return 0;
}

static void free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < cluster_count + 2) {
        uint32_t next = fat_get(cluster);
        fat_set(cluster, 0);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) break;
        cluster = next;
    }
}

static bool make_short_name(const char *path, uint8_t output[11]) {
    if (!path || path[0] != '/') return false;
    ++path;
    if (!*path || strchr(path, '/')) return false;
    memset(output, ' ', 11);
    size_t base = 0, extension = 0;
    bool in_extension = false;
    for (; *path; ++path) {
        if (*path == '.') {
            if (in_extension || base == 0) return false;
            in_extension = true;
            continue;
        }
        unsigned char character = (unsigned char)*path;
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_' || character == '-'))
            return false;
        if (!in_extension) {
            if (base == 8) return false;
            output[base++] = character;
        } else {
            if (extension == 3) return false;
            output[8 + extension++] = character;
        }
    }
    return base != 0;
}

static struct fat_directory_entry *find_entry(const uint8_t name[11]) {
    uint32_t cluster = 2;
    for (;;) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t i = 0; i < FAT_SECTOR_SIZE / sizeof(*entries); ++i) {
            if (entries[i].name[0] == 0) return 0;
            if (entries[i].name[0] != 0xe5 && !(entries[i].attributes & FAT_ATTR_VOLUME) &&
                memcmp(entries[i].name, name, 11) == 0) return &entries[i];
        }
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) return 0;
        cluster = next;
    }
}

static struct fat_directory_entry *create_entry(const uint8_t name[11]) {
    uint32_t cluster = 2;
    for (;;) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t i = 0; i < FAT_SECTOR_SIZE / sizeof(*entries); ++i) {
            if (entries[i].name[0] != 0 && entries[i].name[0] != 0xe5) continue;
            memset(&entries[i], 0, sizeof(entries[i]));
            memcpy(entries[i].name, name, 11);
            entries[i].attributes = 0x20;
            return &entries[i];
        }
        uint32_t next = fat_get(cluster);
        if (next < FAT_EOC_MIN && next >= 2 && next < cluster_count + 2) {
            cluster = next;
            continue;
        }
        uint32_t added = allocate_cluster();
        if (!added) return 0;
        fat_set(cluster, added);
        struct fat_directory_entry *entry = (void *)cluster_data(added);
        memcpy(entry->name, name, 11);
        entry->attributes = 0x20;
        return entry;
    }
}

static struct fat_open_file *allocate_open_file(struct fat_directory_entry *entry) {
    for (size_t i = 0; i < FAT_MAX_OPEN_FILES; ++i) {
        if (open_files[i].used) continue;
        open_files[i].entry = entry;
        open_files[i].used = true;
        return &open_files[i];
    }
    return 0;
}

static int fat_open(const char *path, uint32_t flags, void *data, void **node) {
    (void)data;
    uint8_t name[11];
    if (!node || !make_short_name(path, name)) return -1;
    struct fat_directory_entry *entry = find_entry(name);
    if (!entry && !(flags & VFS_OPEN_CREATE)) return -1;
    if (!entry) entry = create_entry(name);
    if (!entry || (entry->attributes & FAT_ATTR_DIRECTORY)) return -1;
    struct fat_open_file *file = allocate_open_file(entry);
    if (!file) return -1;
    if (flags & VFS_OPEN_TRUNCATE) {
        free_chain(entry_cluster(entry));
        set_entry_cluster(entry, 0);
        entry->size = 0;
    }
    *node = file;
    return 0;
}

static int fat_read(void *node, uint64_t offset, void *buffer, size_t length) {
    struct fat_open_file *file = node;
    if (!file || !file->used || !file->entry || (!buffer && length)) return -1;
    struct fat_directory_entry *entry = file->entry;
    if (offset >= entry->size) return 0;
    if (length > entry->size - offset) length = entry->size - (size_t)offset;
    if (length > INT_MAX) length = INT_MAX;
    uint32_t cluster = entry_cluster(entry);
    uint64_t skip = offset / FAT_SECTOR_SIZE;
    while (skip-- && cluster >= 2 && cluster < cluster_count + 2)
        cluster = fat_get(cluster);
    size_t copied = 0;
    size_t within = offset % FAT_SECTOR_SIZE;
    while (copied < length && cluster >= 2 && cluster < cluster_count + 2) {
        size_t chunk = FAT_SECTOR_SIZE - within;
        if (chunk > length - copied) chunk = length - copied;
        memcpy((uint8_t *)buffer + copied, cluster_data(cluster) + within, chunk);
        copied += chunk;
        within = 0;
        if (copied < length) cluster = fat_get(cluster);
    }
    return (int)copied;
}

static bool ensure_chain(struct fat_directory_entry *entry, size_t needed) {
    if (!needed) return true;
    uint32_t cluster = entry_cluster(entry);
    if (cluster < 2 || cluster >= cluster_count + 2) {
        cluster = allocate_cluster();
        if (!cluster) return false;
        set_entry_cluster(entry, cluster);
    }
    size_t present = 1;
    while (present < needed) {
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) {
            next = allocate_cluster();
            if (!next) return false;
            fat_set(cluster, next);
        }
        cluster = next;
        ++present;
    }
    return true;
}

static uint32_t chain_at(struct fat_directory_entry *entry, size_t index) {
    uint32_t cluster = entry_cluster(entry);
    while (index-- && cluster >= 2 && cluster < cluster_count + 2)
        cluster = fat_get(cluster);
    return cluster;
}

static void write_bytes(struct fat_directory_entry *entry, uint64_t offset,
                        const uint8_t *source, size_t length) {
    size_t copied = 0;
    while (copied < length) {
        uint64_t position = offset + copied;
        uint32_t cluster = chain_at(entry, position / FAT_SECTOR_SIZE);
        size_t within = position % FAT_SECTOR_SIZE;
        size_t chunk = FAT_SECTOR_SIZE - within;
        if (chunk > length - copied) chunk = length - copied;
        memcpy(cluster_data(cluster) + within, source + copied, chunk);
        copied += chunk;
    }
}

static int fat_write(void *node, uint64_t offset, const void *buffer, size_t length) {
    struct fat_open_file *file = node;
    if (!file || !file->used || !file->entry || (!buffer && length) ||
        offset > UINT32_MAX || length > INT_MAX ||
        length > UINT32_MAX - offset) return -1;
    struct fat_directory_entry *entry = file->entry;
    if (length == 0) return 0;
    uint64_t new_end = offset + length;
    size_t needed = (size_t)((new_end + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE);
    if (needed > cluster_count) return -1;
    if (!ensure_chain(entry, needed)) return -1;
    if (offset > entry->size) {
        static const uint8_t zeros[FAT_SECTOR_SIZE];
        uint64_t hole = entry->size;
        while (hole < offset) {
            size_t chunk = offset - hole > sizeof(zeros) ? sizeof(zeros) : offset - hole;
            write_bytes(entry, hole, zeros, chunk);
            hole += chunk;
        }
    }
    write_bytes(entry, offset, buffer, length);
    if (new_end > entry->size) entry->size = (uint32_t)new_end;
    return (int)length;
}

static int fat_unlink(const char *path, void *data) {
    (void)data;
    uint8_t name[11];
    if (!make_short_name(path, name)) return -1;
    struct fat_directory_entry *entry = find_entry(name);
    if (!entry) return -1;
    for (size_t i = 0; i < FAT_MAX_OPEN_FILES; ++i)
        if (open_files[i].used && open_files[i].entry == entry) return -1;
    free_chain(entry_cluster(entry));
    entry->name[0] = 0xe5;
    entry->size = 0;
    return 0;
}

static void fat_close(void *node) {
    struct fat_open_file *file = node;
    if (!file || !file->used) return;
    file->entry = 0;
    file->used = false;
}

static void format_volume(void) {
    memset(ram_volume, 0, sizeof(ram_volume));
    /* This FAT has spare entries, so it can index every data cluster. */
    fat_sectors = 1024;
    cluster_count = RAM_TOTAL_SECTORS - FAT_RESERVED_SECTORS - FAT_COUNT * fat_sectors;
    data_start_sector = FAT_RESERVED_SECTORS + FAT_COUNT * fat_sectors;
    uint8_t *boot = sector(0);
    boot[0] = 0xeb; boot[1] = 0x58; boot[2] = 0x90;
    memcpy(boot + 3, "UNITASOS", 8);
    boot[11] = 0x00; boot[12] = 0x02;
    boot[13] = 1;
    boot[14] = FAT_RESERVED_SECTORS; boot[15] = 0;
    boot[16] = FAT_COUNT;
    boot[17] = 0; boot[18] = 0;
    boot[19] = 0; boot[20] = 0;
    boot[21] = 0xf8;
    boot[22] = 0; boot[23] = 0;
    boot[24] = 63; boot[25] = 0;
    boot[26] = 255; boot[27] = 0;
    store_le32(boot + 28, 0);
    store_le32(boot + 32, RAM_TOTAL_SECTORS);
    store_le32(boot + 36, fat_sectors);
    boot[40] = 0; boot[41] = 0;
    boot[42] = 0; boot[43] = 0;
    store_le32(boot + 44, 2);
    boot[48] = 1; boot[49] = 0;
    boot[50] = 6; boot[51] = 0;
    boot[64] = 0x80; boot[66] = 0x29;
    store_le32(boot + 67, 0x554e4954);
    memcpy(boot + 71, "UNITASOS   ", 11);
    memcpy(boot + 82, "FAT32   ", 8);
    boot[510] = 0x55; boot[511] = 0xaa;
    memcpy(sector(6), boot, FAT_SECTOR_SIZE);

    uint8_t *info = sector(1);
    store_le32(info, 0x41615252);
    store_le32(info + 484, 0x61417272);
    /* FSInfo free-cluster hints are unknown because writes do not update them. */
    store_le32(info + 488, 0xffffffff);
    store_le32(info + 492, 0xffffffff);
    store_le32(info + 508, 0xaa550000);
    memcpy(sector(7), info, FAT_SECTOR_SIZE);

    fat_set(0, 0x0ffffff8);
    fat_set(1, FAT_EOC);
    fat_set(2, FAT_EOC);
    next_free_cluster = 3;
}

static int fat_readdir(const char *path, void *data, uint64_t index,
                       struct vfs_dirent *output) {
    (void)data;
    if (strcmp(path, "/") != 0 || !output) return -1;
    uint32_t cluster = 2;
    uint64_t visible = 0;
    for (;;) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t i = 0; i < FAT_SECTOR_SIZE / sizeof(*entries); ++i) {
            struct fat_directory_entry *entry = &entries[i];
            if (entry->name[0] == 0) return 0;
            if (entry->name[0] == 0xe5 || (entry->attributes & FAT_ATTR_VOLUME)) continue;
            if (visible++ != index) continue;
            size_t out = 0, base_length = 8;
            while (base_length && entry->name[base_length - 1] == ' ') --base_length;
            for (size_t j = 0; j < base_length; ++j) output->name[out++] = entry->name[j];
            size_t extension_length = 3;
            while (extension_length && entry->name[8 + extension_length - 1] == ' ') --extension_length;
            if (extension_length) {
                output->name[out++] = '.';
                for (size_t j = 0; j < extension_length; ++j) output->name[out++] = entry->name[8 + j];
            }
            output->name[out] = 0;
            output->type = entry->attributes & FAT_ATTR_DIRECTORY ? 1 : 0;
            output->size = entry->size;
            return 1;
        }
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) return 0;
        cluster = next;
    }
}

static const struct filesystem ram_fat32 = {
    .name = "fat32-ram",
    .open = fat_open,
    .read = fat_read,
    .write = fat_write,
    .close = fat_close,
    .unlink = fat_unlink,
    .readdir = fat_readdir
};

int ramfs_init(void) {
    format_volume();
    if (vfs_register(&ram_fat32) != 0) return -1;
    return vfs_mount("/", &ram_fat32, 0);
}

size_t ramfs_capacity_bytes(void) { return sizeof(ram_volume); }
