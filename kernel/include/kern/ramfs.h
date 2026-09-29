#ifndef UNITAS_KERN_RAMFS_H
#define UNITAS_KERN_RAMFS_H

#include <stddef.h>
#include <stdint.h>

/* Formats a fixed in-memory volume as FAT32 and mounts it at the VFS root. */
int ramfs_init(void);
size_t ramfs_capacity_bytes(void);

#endif
