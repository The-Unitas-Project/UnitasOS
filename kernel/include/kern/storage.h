#ifndef UNITAS_KERN_STORAGE_H
#define UNITAS_KERN_STORAGE_H

int ide_init(void);
int ahci_init(void);
int nvme_init(void);
void storage_drivers_init(void);

#endif
