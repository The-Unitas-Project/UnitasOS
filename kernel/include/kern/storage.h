#ifndef UNITAS_KERN_STORAGE_H
#define UNITAS_KERN_STORAGE_H

/* Probe each controller family and register discovered block devices. */
int ide_init(void);
int ahci_init(void);
int nvme_init(void);
/* Register and initialize all storage drivers. */
void storage_drivers_init(void);

#endif
