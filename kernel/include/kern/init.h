#ifndef UNITAS_KERN_INIT_H
#define UNITAS_KERN_INIT_H

#include <stddef.h>

typedef int (*init_path_probe_t)(const char *path, void *context);

/* Return 1 when the path is a regular file. Return 0 otherwise. */
const char *unitas_select_init(init_path_probe_t probe, void *context);

#endif
