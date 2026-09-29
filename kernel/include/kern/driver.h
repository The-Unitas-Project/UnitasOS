#ifndef UNITAS_KERN_DRIVER_H
#define UNITAS_KERN_DRIVER_H

#include <stddef.h>

/* The registry stores this pointer; keep it valid while the driver is registered. */
struct driver {
    const char *name;
    int (*init)(void);
    void (*shutdown)(void);
};

/* Calls init before storing the descriptor. A failed init leaves it unregistered. */
int driver_register(const struct driver *driver);
const struct driver *driver_find(const char *name);
size_t driver_count(void);
void platform_drivers_init(void);

#endif
