#include <kern/ramfs.h>
#include <kern/string.h>
#include <kern/types.h>
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
#define FAT_ATTR_LONG_NAME 0x0fu

/* This RAM volume uses 512-byte clusters and printable ASCII long names. */
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

struct fat_long_entry {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attributes;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster_low;
    uint16_t name3[2];
} __attribute__((packed));

#define FAT_MAX_OPEN_FILES 64
struct fat_open_file {
    struct fat_directory_entry *entry;
    bool used;
};

_Static_assert(sizeof(struct fat_directory_entry) == 32, "FAT directory entry size");
_Static_assert(sizeof(struct fat_long_entry) == 32, "FAT long name entry size");

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
    /* Clear each FAT link before following it. This also breaks cycles. */
    while (cluster >= 2 && cluster < cluster_count + 2) {
        uint32_t next = fat_get(cluster);
        fat_set(cluster, 0);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) break;
        cluster = next;
    }
}

static bool make_short_component(const char *path, size_t length,
                                 uint8_t output[11]) {
    if (!path || !length || (length == 1 && path[0] == '.') ||
        (length == 2 && path[0] == '.' && path[1] == '.')) return false;
    memset(output, ' ', 11);
    size_t base = 0, extension = 0;
    bool in_extension = false;
    for (size_t index = 0; index < length; ++index) {
        if (path[index] == '.') {
            if (in_extension || base == 0) return false;
            in_extension = true;
            continue;
        }
        unsigned char character = (unsigned char)path[index];
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

static struct fat_directory_entry *find_entry(uint32_t directory,
                                               const uint8_t name[11]) {
    if (directory < 2 || directory >= cluster_count + 2) return 0;
    uint32_t cluster = directory;
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t i = 0; i < FAT_SECTOR_SIZE / sizeof(*entries); ++i) {
            if (entries[i].name[0] == 0) return 0;
            if (entries[i].name[0] != 0xe5 && !(entries[i].attributes & FAT_ATTR_VOLUME) &&
                memcmp(entries[i].name, name, 11) == 0) return &entries[i];
        }
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) break;
        cluster = next;
    }
    return 0;
}

static uint8_t short_name_checksum(const uint8_t name[11]) {
    uint8_t sum = 0;
    for (size_t index = 0; index < 11; ++index)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name[index]);
    return sum;
}

static bool valid_long_component(const char *name) {
    if (!name) return false;
    size_t length = strlen(name);
    if (!length || length > 255 || name[length - 1] == ' ' ||
        name[length - 1] == '.') return false;
    if ((length == 1 && name[0] == '.') ||
        (length == 2 && name[0] == '.' && name[1] == '.')) return false;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)name[index];
        if (character < 0x20 || character >= 0x7f || character == '"' ||
            character == '*' || character == '/' || character == ':' ||
            character == '<' || character == '>' || character == '?' ||
            character == '\\' || character == '|') return false;
    }
    return true;
}

static bool equal_name(const char *first, const char *second) {
    size_t index = 0;
    while (first[index] && second[index]) {
        char a = first[index];
        char b = second[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (char)(b + 'a' - 'A');
        if (a != b) return false;
        ++index;
    }
    return first[index] == 0 && second[index] == 0;
}

static uint16_t long_name_unit(const struct fat_long_entry *entry,
                               size_t index) {
    if (index < 5) return entry->name1[index];
    if (index < 11) return entry->name2[index - 5];
    return entry->name3[index - 11];
}

static void set_long_name_unit(struct fat_long_entry *entry, size_t index,
                               uint16_t value) {
    if (index < 5) entry->name1[index] = value;
    else if (index < 11) entry->name2[index - 5] = value;
    else entry->name3[index - 11] = value;
}

static struct fat_directory_entry *find_entry_name(uint32_t directory,
                                                     const char *name) {
    uint8_t short_name[11];
    size_t name_length = name ? strlen(name) : 0;
    if (!name || !valid_long_component(name)) return 0;
    if (make_short_component(name, name_length, short_name))
        return find_entry(directory, short_name);
    if (directory < 2 || directory >= cluster_count + 2) return 0;
    /* Decode a long name only when its order and short-name checksum match. */
    char decoded[256];
    memset(decoded, 0xff, sizeof(decoded));
    uint8_t expected_sequence = 0;
    uint8_t checksum = 0;
    bool valid = false;
    uint32_t cluster = directory;
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t index = 0; index < FAT_SECTOR_SIZE / sizeof(*entries); ++index) {
            struct fat_directory_entry *entry = &entries[index];
            if (entry->name[0] == 0) return 0;
            if (entry->name[0] == 0xe5) {
                valid = false;
                continue;
            }
            if (entry->attributes == FAT_ATTR_LONG_NAME) {
                const struct fat_long_entry *part = (const void *)entry;
                uint8_t sequence = part->order & 0x1f;
                if (part->order & 0x40) {
                    memset(decoded, 0xff, sizeof(decoded));
                    expected_sequence = sequence;
                    checksum = part->checksum;
                    valid = sequence != 0;
                }
                if (!valid || sequence != expected_sequence ||
                    part->checksum != checksum) {
                    valid = false;
                    continue;
                }
                size_t offset = (size_t)(sequence - 1) * 13;
                for (size_t unit = 0; unit < 13; ++unit) {
                    uint16_t character = long_name_unit(part, unit);
                    if (offset + unit < sizeof(decoded))
                        decoded[offset + unit] = character == 0 ? 0 :
                            character == 0xffff ? (char)0xff :
                            character < 0x80 ? (char)character : '?';
                }
                --expected_sequence;
                continue;
            }
            if (!(entry->attributes & FAT_ATTR_VOLUME) && valid &&
                expected_sequence == 0 &&
                short_name_checksum(entry->name) == checksum) {
                size_t length = 0;
                while (length < sizeof(decoded) &&
                       (unsigned char)decoded[length] != 0xff && decoded[length])
                    ++length;
                if (length == sizeof(decoded)) decoded[length - 1] = 0;
                else decoded[length] = 0;
                if (equal_name(decoded, name)) return entry;
            }
            valid = false;
        }
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) break;
        cluster = next;
    }
    return 0;
}

static struct fat_directory_entry *create_entry(uint32_t directory,
                                                 const uint8_t name[11]) {
    if (directory < 2 || directory >= cluster_count + 2) return 0;
    uint32_t cluster = directory;
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
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
    return 0;
}

static bool make_short_alias(uint32_t directory, const char *name,
                             uint8_t output[11]) {
    if (!valid_long_component(name)) return false;
    size_t length = strlen(name);
    if (make_short_component(name, length, output))
        return find_entry(directory, output) == 0;
    size_t dot = length;
    for (size_t index = 0; index < length; ++index)
        if (name[index] == '.') dot = index;
    char base[256];
    size_t base_length = 0;
    for (size_t index = 0; index < dot; ++index) {
        unsigned char character = (unsigned char)name[index];
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if ((character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' ||
            character == '-') base[base_length++] = (char)character;
    }
    if (!base_length) base[base_length++] = 'F';
    size_t extension_length = dot < length ? length - dot - 1 : 0;
    memset(output, ' ', 11);
    size_t output_extension = 0;
    for (size_t index = dot + (dot < length); index < length &&
         output_extension < 3; ++index) {
        unsigned char character = (unsigned char)name[index];
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if ((character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9'))
            output[8 + output_extension++] = character;
    }
    (void)extension_length;
    /* Try numbered aliases until the directory has no matching short name. */
    for (unsigned suffix = 1; suffix < 10000; ++suffix) {
        unsigned digits = suffix < 10 ? 1 : suffix < 100 ? 2 :
                          suffix < 1000 ? 3 : 4;
        size_t prefix = 7 - digits;
        if (prefix > base_length) prefix = base_length;
        memset(output, ' ', 8);
        memcpy(output, base, prefix);
        output[prefix] = '~';
        unsigned value = suffix;
        for (unsigned index = 0; index < digits; ++index) {
            output[prefix + digits - index] = (uint8_t)('0' + value % 10);
            value /= 10;
        }
        if (!find_entry(directory, output)) return true;
    }
    return false;
}

static struct fat_directory_entry *create_entry_name(uint32_t directory,
                                                       const char *name) {
    if (!valid_long_component(name)) return 0;
    uint8_t short_name[11];
    if (!make_short_alias(directory, name, short_name)) return 0;
    size_t length = strlen(name);
    if (make_short_component(name, length, short_name))
        return create_entry(directory, short_name);
    /* Store long-name records before the short alias, in reverse order. */
    size_t count = (length + 1 + 12) / 13;
    struct fat_directory_entry *parts[20];
    if (count > ARRAY_SIZE(parts)) return 0;
    uint8_t checksum = short_name_checksum(short_name);
    size_t created = 0;
    for (size_t sequence = count; sequence > 0; --sequence) {
        struct fat_directory_entry *slot = create_entry(directory, short_name);
        if (!slot) {
            for (size_t index = 0; index < created; ++index)
                parts[index]->name[0] = 0xe5;
            return 0;
        }
        parts[created++] = slot;
        struct fat_long_entry record;
        memset(&record, 0xff, sizeof(record));
        record.order = (uint8_t)sequence;
        if (sequence == count) record.order |= 0x40;
        record.attributes = FAT_ATTR_LONG_NAME;
        record.type = 0;
        record.checksum = checksum;
        record.first_cluster_low = 0;
        size_t start = (sequence - 1) * 13;
        for (size_t unit = 0; unit < 13; ++unit) {
            size_t position = start + unit;
            uint16_t character = position < length ?
                (uint8_t)name[position] : position == length ? 0 : 0xffff;
            set_long_name_unit(&record, unit, character);
        }
        memcpy(slot, &record, sizeof(record));
    }
    struct fat_directory_entry *entry = create_entry(directory, short_name);
    if (!entry) {
        for (size_t index = 0; index < created; ++index)
            parts[index]->name[0] = 0xe5;
    }
    return entry;
}

static void delete_name_entries(uint32_t directory,
                                struct fat_directory_entry *target) {
    struct fat_directory_entry *parts[20];
    size_t part_count = 0;
    uint32_t cluster = directory;
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t index = 0; index < FAT_SECTOR_SIZE / sizeof(*entries); ++index) {
            struct fat_directory_entry *entry = &entries[index];
            if (entry->name[0] == 0) return;
            if (entry->name[0] == 0xe5) {
                part_count = 0;
                continue;
            }
            if (entry->attributes == FAT_ATTR_LONG_NAME) {
                if (part_count < ARRAY_SIZE(parts)) parts[part_count++] = entry;
                else part_count = 0;
                continue;
            }
            if (entry == target) {
                for (size_t part = 0; part < part_count; ++part)
                    parts[part]->name[0] = 0xe5;
                target->name[0] = 0xe5;
                return;
            }
            part_count = 0;
        }
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) return;
        cluster = next;
    }
}

static bool create_directory(uint32_t parent, const char *name) {
    if (find_entry_name(parent, name)) return false;
    struct fat_directory_entry *entry = create_entry_name(parent, name);
    if (!entry) return false;
    uint32_t cluster = allocate_cluster();
    if (!cluster) {
        delete_name_entries(parent, entry);
        return false;
    }
    entry->attributes = FAT_ATTR_DIRECTORY;
    set_entry_cluster(entry, cluster);

    struct fat_directory_entry *items = (void *)cluster_data(cluster);
    memcpy(items[0].name, ".          ", sizeof(items[0].name));
    items[0].attributes = FAT_ATTR_DIRECTORY;
    set_entry_cluster(&items[0], cluster);
    memcpy(items[1].name, "..         ", sizeof(items[1].name));
    items[1].attributes = FAT_ATTR_DIRECTORY;
    set_entry_cluster(&items[1], parent);
    return true;
}

static bool resolve_parent(const char *path, uint32_t *parent_cluster,
                           char leaf_name[256]) {
    /* VFS paths are absolute. Return the parent cluster and final component. */
    if (!path || path[0] != '/' || !path[1] || !parent_cluster || !leaf_name)
        return false;
    const char *component = path + 1;
    uint32_t directory = 2;
    for (;;) {
        const char *end = component;
        while (*end && *end != '/') ++end;
        size_t length = (size_t)(end - component);
        if (!length || length >= 256) return false;
        char name[256];
        memcpy(name, component, length);
        name[length] = 0;
        if (!valid_long_component(name)) return false;
        if (!*end) {
            *parent_cluster = directory;
            memcpy(leaf_name, name, length + 1);
            return true;
        }
        if (!end[1]) return false;
        struct fat_directory_entry *entry = find_entry_name(directory, name);
        if (!entry || !(entry->attributes & FAT_ATTR_DIRECTORY)) return false;
        directory = entry_cluster(entry);
        if (directory < 2 || directory >= cluster_count + 2) return false;
        component = end + 1;
    }
}

static bool resolve_directory(const char *path, uint32_t *directory) {
    if (!path || path[0] != '/' || !directory) return false;
    if (!path[1]) {
        *directory = 2;
        return true;
    }
    uint32_t parent;
    char name[256];
    if (!resolve_parent(path, &parent, name)) return false;
    struct fat_directory_entry *entry = find_entry_name(parent, name);
    if (!entry || !(entry->attributes & FAT_ATTR_DIRECTORY)) return false;
    *directory = entry_cluster(entry);
    return *directory >= 2 && *directory < cluster_count + 2;
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
    uint32_t parent;
    char name[256];
    if (!node || !resolve_parent(path, &parent, name)) return -1;
    struct fat_directory_entry *entry = find_entry_name(parent, name);
    if (!entry && !(flags & VFS_OPEN_CREATE)) return -1;
    if (!entry) entry = create_entry_name(parent, name);
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
    /* Extend the file chain to the requested number of data clusters. */
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
    /* FAT has no sparse-file marker, so clear every gap before the write. */
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
    uint32_t parent;
    char name[256];
    if (!resolve_parent(path, &parent, name)) return -1;
    struct fat_directory_entry *entry = find_entry_name(parent, name);
    if (!entry || (entry->attributes & FAT_ATTR_DIRECTORY)) return -1;
    for (size_t i = 0; i < FAT_MAX_OPEN_FILES; ++i)
        if (open_files[i].used && open_files[i].entry == entry) return -1;
    free_chain(entry_cluster(entry));
    delete_name_entries(parent, entry);
    entry->size = 0;
    return 0;
}

static int fat_mkdir(const char *path, uint32_t mode, void *data) {
    (void)mode;
    (void)data;
    uint32_t parent;
    char name[256];
    if (!resolve_parent(path, &parent, name) || find_entry_name(parent, name))
        return -1;
    return create_directory(parent, name) ? 0 : -1;
}

static int fat_rmdir(const char *path, void *data) {
    (void)data;
    uint32_t parent;
    char name[256];
    if (!resolve_parent(path, &parent, name)) return -1;
    struct fat_directory_entry *entry = find_entry_name(parent, name);
    if (!entry || !(entry->attributes & FAT_ATTR_DIRECTORY)) return -1;
    uint32_t cluster = entry_cluster(entry);
    if (cluster < 2 || cluster >= cluster_count + 2) return -1;
    uint32_t current = cluster;
    bool chain_ended = false;
    /* Do not free a directory if its FAT chain is invalid or cyclic. */
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
        struct fat_directory_entry *items = (void *)cluster_data(current);
        for (size_t index = 0; index < FAT_SECTOR_SIZE / sizeof(*items); ++index) {
            if (items[index].name[0] == 0) break;
            if (items[index].name[0] == 0xe5 || items[index].name[0] == '.')
                continue;
            return -1;
        }
        uint32_t next = fat_get(current);
        if (next >= FAT_EOC_MIN) {
            chain_ended = true;
            break;
        }
        if (next < 2 || next >= cluster_count + 2) return -1;
        current = next;
    }
    if (!chain_ended) return -1;
    free_chain(cluster);
    delete_name_entries(parent, entry);
    entry->size = 0;
    return 0;
}

static int fat_rename(const char *source_path, const char *destination_path,
                      void *data) {
    (void)data;
    if (!source_path || !destination_path) return -1;
    if (strcmp(source_path, destination_path) == 0) return 0;
    uint32_t source_parent, destination_parent;
    char source_name[256], destination_name[256];
    if (!resolve_parent(source_path, &source_parent, source_name) ||
        !resolve_parent(destination_path, &destination_parent, destination_name))
        return -1;
    struct fat_directory_entry *source =
        find_entry_name(source_parent, source_name);
    if (!source) return -1;
    bool is_directory = (source->attributes & FAT_ATTR_DIRECTORY) != 0;
    if (is_directory) {
        uint32_t child_cluster = entry_cluster(source);
        uint32_t ancestor = destination_parent;
        bool reached_root = false;
        for (uint32_t depth = 0; depth < cluster_count; ++depth) {
            if (ancestor == child_cluster) return -1;
            if (ancestor == 2) {
                reached_root = true;
                break;
            }
            if (ancestor < 2 || ancestor >= cluster_count + 2) return -1;
            struct fat_directory_entry *items = (void *)cluster_data(ancestor);
            if (items[1].name[0] != '.') return -1;
            uint32_t parent_cluster = entry_cluster(&items[1]);
            if (parent_cluster == ancestor) return -1;
            ancestor = parent_cluster;
        }
        if (!reached_root) return -1;
    }
    struct fat_directory_entry *destination =
        find_entry_name(destination_parent, destination_name);
    if (destination == source) return 0;
    if (destination) {
        bool destination_is_directory =
            (destination->attributes & FAT_ATTR_DIRECTORY) != 0;
        if (is_directory || destination_is_directory) return -1;
        for (size_t index = 0; index < FAT_MAX_OPEN_FILES; ++index)
            if (open_files[index].used && open_files[index].entry == destination)
                return -1;
    }
    if (!is_directory) {
        for (size_t index = 0; index < FAT_MAX_OPEN_FILES; ++index)
            if (open_files[index].used && open_files[index].entry == source)
                return -1;
    }
    /* Create the destination first so a failed allocation keeps the source. */
    struct fat_directory_entry saved = *source;
    uint32_t replaced_cluster = 0;
    if (destination) {
        replaced_cluster = entry_cluster(destination);
        uint8_t alias[sizeof(destination->name)];
        memcpy(alias, destination->name, sizeof(alias));
        *destination = saved;
        memcpy(destination->name, alias, sizeof(alias));
    } else {
        destination = create_entry_name(destination_parent, destination_name);
        if (!destination) return -1;
        uint8_t alias[sizeof(destination->name)];
        memcpy(alias, destination->name, sizeof(alias));
        *destination = saved;
        memcpy(destination->name, alias, sizeof(alias));
    }
    if (is_directory) {
        uint32_t child_cluster = entry_cluster(destination);
        if (child_cluster < 2 || child_cluster >= cluster_count + 2) {
            delete_name_entries(destination_parent, destination);
            return -1;
        }
        struct fat_directory_entry *items = (void *)cluster_data(child_cluster);
        set_entry_cluster(&items[1], destination_parent);
    }
    delete_name_entries(source_parent, source);
    if (replaced_cluster >= 2 && replaced_cluster < cluster_count + 2)
        free_chain(replaced_cluster);
    return 0;
}

static int fat_truncate(void *node, uint64_t size) {
    struct fat_open_file *file = node;
    if (!file || !file->used || !file->entry || size > UINT32_MAX) return -1;
    struct fat_directory_entry *entry = file->entry;
    if (entry->attributes & FAT_ATTR_DIRECTORY) return -1;
    uint64_t old_size = entry->size;
    /* Shrink by detaching the tail. Zero bytes added during growth. */
    if (size < old_size) {
        size_t keep = (size_t)((size + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE);
        uint32_t first = entry_cluster(entry);
        if (!keep) {
            free_chain(first);
            set_entry_cluster(entry, 0);
        } else {
            uint32_t last = first;
            for (size_t index = 1; index < keep; ++index) {
                if (last < 2 || last >= cluster_count + 2) return -1;
                last = fat_get(last);
            }
            if (last < 2 || last >= cluster_count + 2) return -1;
            uint32_t following = fat_get(last);
            fat_set(last, FAT_EOC);
            if (following < FAT_EOC_MIN && following >= 2 &&
                following < cluster_count + 2) free_chain(following);
        }
    } else if (size > old_size) {
        size_t needed = (size_t)((size + FAT_SECTOR_SIZE - 1) /
                                 FAT_SECTOR_SIZE);
        if (needed > cluster_count || !ensure_chain(entry, needed)) return -1;
        static const uint8_t zeros[FAT_SECTOR_SIZE];
        uint64_t position = old_size;
        while (position < size) {
            size_t amount = (size_t)(size - position);
            if (amount > sizeof(zeros)) amount = sizeof(zeros);
            write_bytes(entry, position, zeros, amount);
            position += amount;
        }
    }
    entry->size = (uint32_t)size;
    return 0;
}

static void fat_fill_stat(const struct fat_directory_entry *entry,
                          struct vfs_stat *result) {
    memset(result, 0, sizeof(*result));
    result->inode = entry ? (uint64_t)((const uint8_t *)entry - ram_volume) : 1;
    result->links = 1;
    result->block_size = FAT_SECTOR_SIZE;
    if (!entry) {
        result->mode = 0040000 | 0755;
        result->links = 2;
        return;
    }
    if (entry->attributes & FAT_ATTR_DIRECTORY) {
        result->mode = 0040000 | 0755;
        result->links = 2;
    } else {
        result->mode = 0100000 | 0644;
        result->size = entry->size;
        result->blocks = ((uint64_t)entry->size + FAT_SECTOR_SIZE - 1) /
                         FAT_SECTOR_SIZE;
    }
}

static int fat_stat_path(const char *path, void *data, struct vfs_stat *result) {
    (void)data;
    if (!path || !result) return -1;
    if (strcmp(path, "/") == 0) {
        fat_fill_stat(0, result);
        return 0;
    }
    uint32_t parent;
    char name[256];
    if (!resolve_parent(path, &parent, name)) return -1;
    struct fat_directory_entry *entry = find_entry_name(parent, name);
    if (!entry) return -1;
    fat_fill_stat(entry, result);
    return 0;
}

static int fat_stat_node(void *node, struct vfs_stat *result) {
    struct fat_open_file *file = node;
    if (!file || !file->used || !file->entry || !result) return -1;
    fat_fill_stat(file->entry, result);
    return 0;
}

static void fat_close(void *node) {
    struct fat_open_file *file = node;
    if (!file || !file->used) return;
    file->entry = 0;
    file->used = false;
}

static bool format_volume(void) {
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

    /* Create standard root paths before the kernel mounts the volume. */
    static const char *const root_directories[] = {
        "bin", "boot", "dev", "etc", "home", "lib", "lib64", "media",
        "mnt", "opt", "proc", "root", "run", "sbin", "srv", "sys",
        "tmp", "usr", "var"
    };
    for (size_t index = 0; index < ARRAY_SIZE(root_directories); ++index)
        if (!create_directory(2, root_directories[index])) return false;
    static const char *const usr_directories[] = {
        "bin", "sbin", "lib", "lib64", "include", "libexec", "share",
        "local"
    };
    struct fat_directory_entry *usr = find_entry_name(2, "usr");
    if (!usr) return false;
    uint32_t usr_cluster = entry_cluster(usr);
    for (size_t index = 0; index < ARRAY_SIZE(usr_directories); ++index)
        if (!create_directory(usr_cluster, usr_directories[index])) return false;
    struct fat_directory_entry *share = find_entry_name(usr_cluster, "share");
    if (!share) return false;
    uint32_t share_cluster = entry_cluster(share);
    static const char *const share_directories[] = { "info", "locale", "man" };
    for (size_t index = 0; index < ARRAY_SIZE(share_directories); ++index)
        if (!create_directory(share_cluster, share_directories[index])) return false;
    static const char *const var_directories[] = { "cache", "lib", "log", "tmp" };
    struct fat_directory_entry *var = find_entry_name(2, "var");
    if (!var) return false;
    uint32_t var_cluster = entry_cluster(var);
    for (size_t index = 0; index < ARRAY_SIZE(var_directories); ++index)
        if (!create_directory(var_cluster, var_directories[index])) return false;
    struct fat_directory_entry *etc = find_entry_name(2, "etc");
    if (!etc || !create_directory(entry_cluster(etc), "profile.d")) return false;
    return true;
}

static int fat_readdir(const char *path, void *data, uint64_t index,
                       struct vfs_dirent *output) {
    (void)data;
    uint32_t cluster;
    if (!output || !resolve_directory(path, &cluster)) return -1;
    uint64_t visible = 0;
    char long_name[256];
    memset(long_name, 0xff, sizeof(long_name));
    uint8_t expected_sequence = 0;
    uint8_t checksum = 0;
    bool long_name_valid = false;
    for (uint32_t visited = 0; visited < cluster_count; ++visited) {
        struct fat_directory_entry *entries = (void *)cluster_data(cluster);
        for (size_t i = 0; i < FAT_SECTOR_SIZE / sizeof(*entries); ++i) {
            struct fat_directory_entry *entry = &entries[i];
            if (entry->name[0] == 0) return 0;
            if (entry->name[0] == 0xe5) {
                long_name_valid = false;
                continue;
            }
            if (entry->attributes == FAT_ATTR_LONG_NAME) {
                const struct fat_long_entry *part = (const void *)entry;
                uint8_t sequence = part->order & 0x1f;
                if (part->order & 0x40) {
                    memset(long_name, 0xff, sizeof(long_name));
                    expected_sequence = sequence;
                    checksum = part->checksum;
                    long_name_valid = sequence != 0;
                }
                if (!long_name_valid || sequence != expected_sequence ||
                    part->checksum != checksum) {
                    long_name_valid = false;
                    continue;
                }
                size_t offset = (size_t)(sequence - 1) * 13;
                for (size_t unit = 0; unit < 13; ++unit) {
                    uint16_t character = long_name_unit(part, unit);
                    if (offset + unit < sizeof(long_name))
                        long_name[offset + unit] = character == 0 ? 0 :
                            character == 0xffff ? (char)0xff :
                            character < 0x80 ? (char)character : '?';
                }
                --expected_sequence;
                continue;
            }
            if (entry->attributes & FAT_ATTR_VOLUME) {
                long_name_valid = false;
                continue;
            }
            char display_name[256];
            if (long_name_valid && expected_sequence == 0 &&
                short_name_checksum(entry->name) == checksum) {
                size_t length = 0;
                while (length < sizeof(long_name) &&
                       (unsigned char)long_name[length] != 0xff &&
                       long_name[length]) ++length;
                if (length == sizeof(long_name)) length = sizeof(long_name) - 1;
                memcpy(display_name, long_name, length);
                display_name[length] = 0;
            } else {
                size_t out = 0;
                size_t base_length = 8;
                while (base_length && entry->name[base_length - 1] == ' ')
                    --base_length;
                for (size_t j = 0; j < base_length; ++j) {
                    char character = (char)entry->name[j];
                    if (character >= 'A' && character <= 'Z')
                        character = (char)(character - 'A' + 'a');
                    display_name[out++] = character;
                }
                size_t extension_length = 3;
                while (extension_length &&
                       entry->name[8 + extension_length - 1] == ' ')
                    --extension_length;
                if (extension_length) {
                    display_name[out++] = '.';
                    for (size_t j = 0; j < extension_length; ++j) {
                        char character = (char)entry->name[8 + j];
                        if (character >= 'A' && character <= 'Z')
                            character = (char)(character - 'A' + 'a');
                        display_name[out++] = character;
                    }
                }
                display_name[out] = 0;
            }
            long_name_valid = false;
            if (entry->name[0] == '.' &&
                (entry->name[1] == ' ' || entry->name[1] == '.')) continue;
            if (visible++ != index) continue;
            memcpy(output->name, display_name, strlen(display_name) + 1);
            output->type = entry->attributes & FAT_ATTR_DIRECTORY ? 1 : 0;
            output->size = entry->size;
            return 1;
        }
        uint32_t next = fat_get(cluster);
        if (next >= FAT_EOC_MIN || next < 2 || next >= cluster_count + 2) return 0;
        cluster = next;
    }
    return 0;
}

static const struct filesystem ram_fat32 = {
    .name = "fat32-ram",
    .open = fat_open,
    .read = fat_read,
    .write = fat_write,
    .close = fat_close,
    .unlink = fat_unlink,
    .stat_path = fat_stat_path,
    .stat_node = fat_stat_node,
    .mkdir = fat_mkdir,
    .rmdir = fat_rmdir,
    .rename = fat_rename,
    .truncate = fat_truncate,
    .readdir = fat_readdir
};

int ramfs_init(void) {
    if (!format_volume()) return -1;
    if (vfs_register(&ram_fat32) != 0) return -1;
    return vfs_mount("/", &ram_fat32, 0);
}

size_t ramfs_capacity_bytes(void) { return sizeof(ram_volume); }
