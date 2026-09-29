#include <kern/driver.h>
#include <kern/string.h>
#include <kern/types.h>

#define MAX_DRIVERS 64
static const struct driver *drivers[MAX_DRIVERS];
static size_t registered_count;

int driver_register(const struct driver *driver) {
    if (!driver || !driver->name || registered_count == MAX_DRIVERS) return -1;
    for (size_t i = 0; i < registered_count; ++i)
        if (drivers[i] == driver || strcmp(drivers[i]->name, driver->name) == 0) return -1;
    int result = driver->init ? driver->init() : 0;
    if (result != 0) return result;
    drivers[registered_count++] = driver;
    return 0;
}

const struct driver *driver_find(const char *name) {
    if (!name) return 0;
    for (size_t i = 0; i < registered_count; ++i)
        if (strcmp(drivers[i]->name, name) == 0) return drivers[i];
    return 0;
}

size_t driver_count(void) { return registered_count; }
