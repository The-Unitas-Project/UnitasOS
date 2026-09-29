#ifndef UNITAS_KERN_DRIVER_H
#define UNITAS_KERN_DRIVER_H

#include <stddef.h>

/* Driver objects are static today. Lifecycle hooks leave room for probe/remove. */
struct driver {
    const char *name;
    int (*init)(void);
    void (*shutdown)(void);
};

int driver_register(const struct driver *driver);
const struct driver *driver_find(const char *name);
size_t driver_count(void);
void platform_drivers_init(void);

#endif
