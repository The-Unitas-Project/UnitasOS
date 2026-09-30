#ifndef UNITAS_KERN_E1000_H
#define UNITAS_KERN_E1000_H

#include <stddef.h>

/* Find and initialize the supported Intel 82540EM device. */
int e1000_init(void);
/* Process completed receive descriptors. Call from the kernel poll loop. */
void e1000_poll(void);
/* Send one Ethernet frame. The driver copies it before this call returns. */
int e1000_send(const void *frame, size_t length);

#endif
