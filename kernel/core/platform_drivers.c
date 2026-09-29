#include <kern/driver.h>
#include <kern/keyboard.h>
#include <kern/panic.h>
#include <kern/timer.h>
#include <kern/storage.h>

/* Keep early platform device activation in one place and in dependency order. */
static int init_timer(void) { pit_init(100); return 0; }
static int init_keyboard(void) { keyboard_init(); return 0; }

static const struct driver timer_driver = { "pit", init_timer, 0 };
static const struct driver keyboard_driver = { "ps2-keyboard", init_keyboard, 0 };

void platform_drivers_init(void) {
    if (driver_register(&timer_driver) != 0) panic("failed to register PIT driver");
    if (driver_register(&keyboard_driver) != 0) panic("failed to register keyboard driver");
    storage_drivers_init();
}
