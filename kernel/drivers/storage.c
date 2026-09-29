#include <kern/driver.h>
#include <kern/panic.h>
#include <kern/storage.h>
#include <kern/log.h>
#include <kern/block.h>

static int init_ide(void) { return ide_init(); }
static int init_ahci(void) { return ahci_init(); }
static int init_nvme(void) { return nvme_init(); }

static const struct driver ide_driver = { "ide-pio", init_ide, 0 };
static const struct driver ahci_driver = { "ahci-sata", init_ahci, 0 };
static const struct driver nvme_driver = { "nvme", init_nvme, 0 };

void storage_drivers_init(void) {
    if (driver_register(&ide_driver) != 0) panic("IDE driver initialization failed");
    if (driver_register(&ahci_driver) != 0) panic("AHCI driver initialization failed");
    if (driver_register(&nvme_driver) != 0) panic("NVMe driver initialization failed");
    log_write(LOG_INFO, "block layer registered %llu disk(s)\n",
              (unsigned long long)block_device_count());
}
