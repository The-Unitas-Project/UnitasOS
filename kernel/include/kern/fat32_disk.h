#ifndef UNITAS_KERN_FAT32_DISK_H
#define UNITAS_KERN_FAT32_DISK_H

#include <stdbool.h>

/* Mount an installed UNITASOS FAT32 partition at /. Return 1, 0, or -1. */
int fat32_disk_mount_root(void);
/* Report whether / uses the disk-backed FAT32 driver. */
bool fat32_disk_root_active(void);

#endif
