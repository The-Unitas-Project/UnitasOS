#ifndef UNITAS_KERN_BLOCK_H
#define UNITAS_KERN_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#define BLOCK_SECTOR_SIZE 512u
#define BLOCK_NAME_MAX 16u
#define BLOCK_MAX_DEVICES 32u

struct block_device {
    char name[BLOCK_NAME_MAX];
    uint64_t sector_count;
    void *private_data;
    int (*read)(struct block_device *device, uint64_t lba, uint32_t count,
                void *buffer);
    int (*write)(struct block_device *device, uint64_t lba, uint32_t count,
                 const void *buffer);
    int (*flush)(struct block_device *device);
};

/* Callbacks use 512-byte sectors and return 0 on success or a negative error. */
/* Read and write buffers may have byte alignment only. */
/* block_read/write check the LBA range before calling the driver. */
/* Registration keeps this pointer; the driver must keep the device alive. */
int block_register(struct block_device *device);
size_t block_device_count(void);
struct block_device *block_device_at(size_t index);
struct block_device *block_find(const char *name);
int block_read(struct block_device *device, uint64_t lba, uint32_t count,
               void *buffer);
int block_write(struct block_device *device, uint64_t lba, uint32_t count,
                const void *buffer);
int block_flush(struct block_device *device);

#endif
