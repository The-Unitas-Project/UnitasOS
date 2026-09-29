#include <assert.h>
#include <kern/block.h>
#include <kern/string.h>
#include <stdio.h>

static uint8_t sectors[4][BLOCK_SECTOR_SIZE];

static int mock_read(struct block_device *device, uint64_t lba,
                     uint32_t count, void *buffer) {
    (void)device;
    memcpy(buffer, sectors[lba], (size_t)count * BLOCK_SECTOR_SIZE);
    return 0;
}

static int mock_write(struct block_device *device, uint64_t lba,
                      uint32_t count, const void *buffer) {
    (void)device;
    memcpy(sectors[lba], buffer, (size_t)count * BLOCK_SECTOR_SIZE);
    return 0;
}

int main(void) {
    struct block_device disk = {
        .name = "mock0",
        .sector_count = 4,
        .read = mock_read,
        .write = mock_write
    };
    uint8_t input[BLOCK_SECTOR_SIZE], output[BLOCK_SECTOR_SIZE];

    memset(input, 0x5a, sizeof(input));
    assert(block_register(&disk) == 0);
    assert(block_device_count() == 1);
    assert(block_find("mock0") == &disk);
    assert(block_write(&disk, 3, 1, input) == 0);
    assert(block_read(&disk, 3, 1, output) == 0);
    assert(memcmp(input, output, sizeof(input)) == 0);
    assert(block_read(&disk, 4, 1, output) < 0);
    assert(block_write(&disk, 3, 2, input) < 0);
    assert(block_read(&disk, 0, 0, output) < 0);
    puts("block layer tests passed");
    return 0;
}
