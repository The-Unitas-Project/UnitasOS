#include <kern/block.h>
#include <kern/string.h>
#include <stdbool.h>

static struct block_device *devices[BLOCK_MAX_DEVICES];
static size_t device_count;

int block_register(struct block_device *device) {
    if (!device || !device->name[0] || !device->sector_count ||
        !device->read || device_count == BLOCK_MAX_DEVICES) return -1;
    for (size_t i = 0; i < device_count; ++i)
        if (strcmp(devices[i]->name, device->name) == 0) return -1;
    devices[device_count++] = device;
    return 0;
}

size_t block_device_count(void) { return device_count; }

struct block_device *block_device_at(size_t index) {
    return index < device_count ? devices[index] : 0;
}

struct block_device *block_find(const char *name) {
    if (!name) return 0;
    for (size_t i = 0; i < device_count; ++i)
        if (strcmp(devices[i]->name, name) == 0) return devices[i];
    return 0;
}

static bool range_valid(const struct block_device *device, uint64_t lba,
                        uint32_t count, const void *buffer) {
    return device && buffer && count && lba < device->sector_count &&
           count <= device->sector_count - lba;
}

int block_read(struct block_device *device, uint64_t lba, uint32_t count,
               void *buffer) {
    if (!range_valid(device, lba, count, buffer)) return -1;
    return device->read(device, lba, count, buffer);
}

int block_write(struct block_device *device, uint64_t lba, uint32_t count,
                const void *buffer) {
    if (!range_valid(device, lba, count, buffer) || !device->write) return -1;
    return device->write(device, lba, count, buffer);
}

int block_flush(struct block_device *device) {
    if (!device || !device->flush) return -1;
    return device->flush(device);
}
