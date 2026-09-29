#include <kern/block.h>
#include <kern/console.h>
#include <kern/devfs.h>
#include <kern/keyboard.h>
#include <kern/serial.h>
#include <kern/string.h>
#include <kern/types.h>
#include <kern/vfs.h>
#include <limits.h>

#ifndef UNITAS_SERIAL
#define UNITAS_SERIAL 0
#endif

#define DEVFS_MAX_OPEN 64

enum devfs_kind { DEV_CONSOLE, DEV_NULL, DEV_ZERO, DEV_KEYBOARD, DEV_SERIAL, DEV_BLOCK };

struct devfs_open_file {
    bool used;
    enum devfs_kind kind;
    struct block_device *block;
};

static struct devfs_open_file open_files[DEVFS_MAX_OPEN];
static uint8_t sector_buffer[BLOCK_SECTOR_SIZE];

static struct devfs_open_file *allocate_file(enum devfs_kind kind,
                                              struct block_device *block) {
    for (size_t i = 0; i < DEVFS_MAX_OPEN; ++i) {
        if (open_files[i].used) continue;
        open_files[i] = (struct devfs_open_file){ true, kind, block };
        return &open_files[i];
    }
    return 0;
}

static int devfs_open(const char *path, uint32_t flags, void *data, void **node) {
    (void)data;
    if (!path || !node || flags) return -1;
    enum devfs_kind kind;
    struct block_device *block = 0;
    if (strcmp(path, "/console") == 0) kind = DEV_CONSOLE;
    else if (strcmp(path, "/null") == 0) kind = DEV_NULL;
    else if (strcmp(path, "/zero") == 0) kind = DEV_ZERO;
    else if (strcmp(path, "/kbd") == 0) kind = DEV_KEYBOARD;
#if UNITAS_SERIAL
    else if (strcmp(path, "/serial0") == 0) kind = DEV_SERIAL;
#endif
    else if (path[0] == '/' && path[1] && !strchr(path + 1, '/') &&
             (block = block_find(path + 1))) kind = DEV_BLOCK;
    else return -1;

    struct devfs_open_file *file = allocate_file(kind, block);
    if (!file) return -1;
    *node = file;
    return 0;
}

static uint64_t block_capacity(const struct block_device *block) {
    if (block->sector_count > UINT64_MAX / BLOCK_SECTOR_SIZE) return UINT64_MAX;
    return block->sector_count * BLOCK_SECTOR_SIZE;
}

static int read_block_bytes(struct block_device *block, uint64_t offset,
                            void *buffer, size_t length) {
    uint64_t capacity = block_capacity(block);
    if (offset >= capacity) return 0;
    if ((uint64_t)length > capacity - offset)
        length = (size_t)(capacity - offset);
    size_t copied = 0;
    while (copied < length) {
        uint64_t position = offset + copied;
        uint64_t lba = position / BLOCK_SECTOR_SIZE;
        size_t within = (size_t)(position % BLOCK_SECTOR_SIZE);
        size_t amount = BLOCK_SECTOR_SIZE - within;
        if (amount > length - copied) amount = length - copied;
        if (block_read(block, lba, 1, sector_buffer) != 0)
            return copied ? (int)copied : -1;
        memcpy((uint8_t *)buffer + copied, sector_buffer + within, amount);
        copied += amount;
    }
    return (int)copied;
}

static int write_block_bytes(struct block_device *block, uint64_t offset,
                             const void *buffer, size_t length) {
    uint64_t capacity = block_capacity(block);
    if (offset >= capacity) return 0;
    if ((uint64_t)length > capacity - offset)
        length = (size_t)(capacity - offset);
    size_t copied = 0;
    while (copied < length) {
        uint64_t position = offset + copied;
        uint64_t lba = position / BLOCK_SECTOR_SIZE;
        size_t within = (size_t)(position % BLOCK_SECTOR_SIZE);
        size_t amount = BLOCK_SECTOR_SIZE - within;
        if (amount > length - copied) amount = length - copied;
        if (within || amount != BLOCK_SECTOR_SIZE) {
            if (block_read(block, lba, 1, sector_buffer) != 0) {
                if (copied && block_flush(block) != 0) return -1;
                return copied ? (int)copied : -1;
            }
        }
        if (within || amount != BLOCK_SECTOR_SIZE)
            memcpy(sector_buffer + within, (const uint8_t *)buffer + copied, amount);
        else
            memcpy(sector_buffer, (const uint8_t *)buffer + copied, BLOCK_SECTOR_SIZE);
        if (block_write(block, lba, 1, sector_buffer) != 0) {
            if (copied && block_flush(block) != 0) return -1;
            return copied ? (int)copied : -1;
        }
        copied += amount;
    }
    if (copied && block_flush(block) != 0) return -1;
    return (int)copied;
}

static int devfs_read(void *node, uint64_t offset, void *buffer, size_t length) {
    struct devfs_open_file *file = node;
    if (!file || !file->used || (!buffer && length) || length > INT_MAX) return -1;
    if (!length) return 0;
    switch (file->kind) {
    case DEV_NULL: return 0;
    case DEV_ZERO:
        memset(buffer, 0, length);
        return (int)length;
    case DEV_CONSOLE:
    case DEV_KEYBOARD:
    case DEV_SERIAL: {
        size_t count = 0;
        while (count < length) {
            char character;
            bool available = file->kind == DEV_KEYBOARD ? keyboard_read_char(&character) :
                             file->kind == DEV_SERIAL ? serial_read_char(&character) :
                             keyboard_read_char(&character) || serial_read_char(&character);
            if (!available) break;
            ((char *)buffer)[count++] = character;
        }
        return (int)count;
    }
    case DEV_BLOCK:
        return read_block_bytes(file->block, offset, buffer, length);
    }
    return -1;
}

static int devfs_write(void *node, uint64_t offset, const void *buffer,
                       size_t length) {
    struct devfs_open_file *file = node;
    if (!file || !file->used || (!buffer && length) || length > INT_MAX) return -1;
    if (!length) return 0;
    switch (file->kind) {
    case DEV_NULL:
    case DEV_ZERO:
        return (int)length;
    case DEV_CONSOLE:
        console_write(buffer, length);
        return (int)length;
    case DEV_SERIAL:
        for (size_t i = 0; i < length; ++i) serial_putc(((const char *)buffer)[i]);
        return (int)length;
    case DEV_KEYBOARD:
        return -1;
    case DEV_BLOCK:
        return file->block->write ?
            write_block_bytes(file->block, offset, buffer, length) : -1;
    }
    return -1;
}

static void devfs_close(void *node) {
    struct devfs_open_file *file = node;
    if (file) *file = (struct devfs_open_file){0};
}

static size_t builtin_count(void) {
#if UNITAS_SERIAL
    return 5;
#else
    return 4;
#endif
}

static const char *builtin_name(size_t index) {
    static const char *const names[] = { "console", "null", "zero", "kbd" };
    if (index < ARRAY_SIZE(names)) return names[index];
#if UNITAS_SERIAL
    if (index == ARRAY_SIZE(names)) return "serial0";
#endif
    return 0;
}

static int devfs_readdir(const char *path, void *data, uint64_t index,
                         struct vfs_dirent *entry) {
    (void)data;
    if (!path || strcmp(path, "/") != 0 || !entry) return -1;
    size_t builtins = builtin_count();
    const char *name = 0;
    uint64_t block_index = index;
    if (index < builtins) name = builtin_name((size_t)index);
    else block_index -= builtins;
    if (!name) {
        if (block_index >= block_device_count()) return 0;
        struct block_device *block = block_device_at((size_t)block_index);
        if (!block) return -1;
        size_t length = strlen(block->name);
        if (!length || length >= sizeof(entry->name)) return -1;
        memcpy(entry->name, block->name, length + 1);
        entry->type = 0;
        entry->size = block_capacity(block);
        return 1;
    }
    size_t length = strlen(name);
    memcpy(entry->name, name, length + 1);
    entry->type = 0;
    entry->size = 0;
    return 1;
}

static const struct filesystem devfs = {
    .name = "devfs",
    .open = devfs_open,
    .read = devfs_read,
    .write = devfs_write,
    .close = devfs_close,
    .readdir = devfs_readdir
};

int devfs_init(void) {
    if (vfs_register(&devfs) != 0) return -1;
    return vfs_mount("/dev", &devfs, 0);
}
