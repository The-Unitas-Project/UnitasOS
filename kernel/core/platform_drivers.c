#include <kern/driver.h>
#include <kern/devfs.h>
#include <kern/keyboard.h>
#include <kern/network.h>
#include <kern/panic.h>
#include <kern/partition.h>
#include <kern/timer.h>
#include <kern/storage.h>
#include <kern/usb.h>

/* Initialize platform devices here, in dependency order. */
static int init_timer(void) { pit_init(100); return 0; }
static int init_keyboard(void) { keyboard_init(); return 0; }
static int init_usb(void) { usb_probe_controllers(); return 0; }
static int init_network(void) { return network_init(); }

static const struct driver timer_driver = { "pit", init_timer, 0 };
static const struct driver keyboard_driver = { "ps2-keyboard", init_keyboard, 0 };
static const struct driver usb_driver = { "usb-probe", init_usb, 0 };
static const struct driver network_driver = { "network", init_network, 0 };
static int init_partitions(void) { return partition_init(); }
static int init_devfs(void) { return devfs_init(); }
static const struct driver partition_driver = { "partitions", init_partitions, 0 };
static const struct driver devfs_driver = { "devfs", init_devfs, 0 };

void platform_storage_init(void) {
    if (driver_register(&timer_driver) != 0) panic("failed to register PIT driver");
    storage_drivers_init();
    if (driver_register(&partition_driver) != 0) panic("failed to scan disk partitions");
}

void platform_drivers_init(void) {
    if (driver_register(&keyboard_driver) != 0) panic("failed to register keyboard driver");
    if (driver_register(&usb_driver) != 0) panic("failed to register USB probe");
    if (driver_register(&network_driver) != 0) panic("failed to initialize networking");
    if (driver_register(&devfs_driver) != 0) panic("failed to mount device files");
}
