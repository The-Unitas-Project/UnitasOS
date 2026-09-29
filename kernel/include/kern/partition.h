#ifndef UNITAS_KERN_PARTITION_H
#define UNITAS_KERN_PARTITION_H

struct block_device;

/* Scan registered disks for GPT or MBR partitions. */
int partition_init(void);
/* Scan one registered disk. Return the number added or a negative error. */
int partition_scan_device(struct block_device *device);

#endif
