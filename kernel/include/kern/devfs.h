#ifndef UNITAS_KERN_DEVFS_H
#define UNITAS_KERN_DEVFS_H

/* Register and mount device files at /dev after drivers register their devices. */
int devfs_init(void);

#endif
