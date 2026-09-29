#include <kern/block.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/storage.h>
#include <kern/string.h>
#include <stdbool.h>

#define IDE_DRIVE_COUNT 4
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_BSY 0x80

struct ide_drive {
    uint16_t io_base;
    uint16_t control_base;
    uint8_t select;
    bool lba48;
    uint64_t sectors;
    struct block_device block;
};

static struct ide_drive drives[IDE_DRIVE_COUNT];
static const uint16_t io_bases[2] = { 0x1f0, 0x170 };
static const uint16_t control_bases[2] = { 0x3f6, 0x376 };

static int wait_status(struct ide_drive *drive, bool require_data) {
    for (uint32_t spin = 0; spin < 1000000; ++spin) {
        uint8_t status = inb(drive->io_base + 7);
        if (status == 0 || status == 0xff) return -1;
        if (status & ATA_STATUS_BSY) continue;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return -1;
        if (!require_data || (status & ATA_STATUS_DRQ)) return 0;
    }
    return -1;
}

static void select_drive(struct ide_drive *drive) {
    outb(drive->io_base + 6, drive->select);
    for (unsigned i = 0; i < 4; ++i) (void)inb(drive->control_base);
}

static int identify_drive(struct ide_drive *drive, uint16_t identify[256]) {
    select_drive(drive);
    outb(drive->io_base + 2, 0);
    outb(drive->io_base + 3, 0);
    outb(drive->io_base + 4, 0);
    outb(drive->io_base + 5, 0);
    outb(drive->io_base + 7, 0xec);
    if (inb(drive->io_base + 7) == 0) return -1;
    if (wait_status(drive, false) != 0) return -1;
    if (inb(drive->io_base + 4) != 0 || inb(drive->io_base + 5) != 0) return -1;
    if (wait_status(drive, true) != 0) return -1;
    for (unsigned i = 0; i < 256; ++i) identify[i] = inw(drive->io_base);
    return 0;
}

static int transfer_sector(struct ide_drive *drive, uint64_t lba,
                           void *buffer, bool write) {
    if (lba >= drive->sectors) return -1;
    if (drive->lba48 && lba >= (1ULL << 48)) return -1;
    select_drive(drive);
    if (wait_status(drive, false) != 0) return -1;
    if (drive->lba48) {
        outb(drive->io_base + 6, (uint8_t)(0x40 | (drive->select & 0x10)));
        outb(drive->io_base + 2, 0);
        outb(drive->io_base + 3, (uint8_t)(lba >> 24));
        outb(drive->io_base + 4, (uint8_t)(lba >> 32));
        outb(drive->io_base + 5, (uint8_t)(lba >> 40));
        outb(drive->io_base + 2, 1);
        outb(drive->io_base + 3, (uint8_t)lba);
        outb(drive->io_base + 4, (uint8_t)(lba >> 8));
        outb(drive->io_base + 5, (uint8_t)(lba >> 16));
        outb(drive->io_base + 7, write ? 0x34 : 0x24);
    } else {
        if (lba >= (1ULL << 28)) return -1;
        outb(drive->io_base + 2, 1);
        outb(drive->io_base + 3, (uint8_t)lba);
        outb(drive->io_base + 4, (uint8_t)(lba >> 8));
        outb(drive->io_base + 5, (uint8_t)(lba >> 16));
        outb(drive->io_base + 6, (uint8_t)(0xe0 | (drive->select & 0x10) | ((lba >> 24) & 0x0f)));
        outb(drive->io_base + 7, write ? 0x30 : 0x20);
    }
    if (wait_status(drive, true) != 0) return -1;
    uint8_t *bytes = buffer;
    if (write) {
        for (unsigned i = 0; i < BLOCK_SECTOR_SIZE / 2; ++i) {
            uint16_t word;
            memcpy(&word, bytes + i * 2, sizeof(word));
            outw(drive->io_base, word);
        }
        if (wait_status(drive, false) != 0) return -1;
    } else {
        for (unsigned i = 0; i < BLOCK_SECTOR_SIZE / 2; ++i) {
            uint16_t word = inw(drive->io_base);
            memcpy(bytes + i * 2, &word, sizeof(word));
        }
    }
    return 0;
}

static int ide_read(struct block_device *device, uint64_t lba, uint32_t count,
                    void *buffer) {
    struct ide_drive *drive = device->private_data;
    for (uint32_t i = 0; i < count; ++i)
        if (transfer_sector(drive, lba + i,
                            (uint8_t *)buffer + (size_t)i * BLOCK_SECTOR_SIZE,
                            false) != 0) return -1;
    return 0;
}

static int ide_write(struct block_device *device, uint64_t lba, uint32_t count,
                     const void *buffer) {
    struct ide_drive *drive = device->private_data;
    for (uint32_t i = 0; i < count; ++i)
        if (transfer_sector(drive, lba + i,
                            (uint8_t *)buffer + (size_t)i * BLOCK_SECTOR_SIZE,
                            true) != 0) return -1;
    return device->flush(device);
}

static int ide_flush(struct block_device *device) {
    struct ide_drive *drive = device->private_data;
    select_drive(drive);
    outb(drive->io_base + 7, drive->lba48 ? 0xea : 0xe7);
    return wait_status(drive, false);
}

static void set_name(char output[BLOCK_NAME_MAX], unsigned index) {
    output[0] = 'i'; output[1] = 'd'; output[2] = 'e'; output[3] = (char)('0' + index);
    output[4] = 0;
}

int ide_init(void) {
    unsigned found = 0;
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned unit = 0; unit < 2; ++unit) {
            struct ide_drive *drive = &drives[channel * 2 + unit];
            uint16_t identify[256];
            drive->io_base = io_bases[channel];
            drive->control_base = control_bases[channel];
            drive->select = (uint8_t)(0xa0 | (unit << 4));
            outb(drive->control_base, 2);
            if (identify_drive(drive, identify) != 0) continue;
            if (!(identify[49] & (1u << 9))) continue;
            drive->lba48 = (identify[83] & 0xc400) == 0x4400;
            if (drive->lba48) {
                drive->sectors = (uint64_t)identify[100] |
                    ((uint64_t)identify[101] << 16) |
                    ((uint64_t)identify[102] << 32) |
                    ((uint64_t)identify[103] << 48);
            } else {
                drive->sectors = (uint32_t)identify[60] |
                    ((uint32_t)identify[61] << 16);
            }
            uint64_t max_sectors = drive->lba48 ? (1ULL << 48) : (1ULL << 28);
            if (!drive->sectors || drive->sectors > max_sectors) continue;
            set_name(drive->block.name, found);
            drive->block.sector_count = drive->sectors;
            drive->block.private_data = drive;
            drive->block.read = ide_read;
            drive->block.write = ide_write;
            drive->block.flush = ide_flush;
            if (block_register(&drive->block) == 0) ++found;
        }
    }
    if (found) log_write(LOG_INFO, "IDE PIO registered %u disk(s)\n", found);
    return 0;
}
