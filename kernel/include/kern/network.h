#ifndef UNITAS_KERN_NETWORK_H
#define UNITAS_KERN_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct network_status {
    bool link_ready;
    bool ipv4_ready;
    uint8_t mac[6];
    uint8_t address[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
};

/* Start the supported network driver. Absence of a device is not an error. */
int network_init(void);
/* Poll the device and advance DHCP state. Call often while the CPU is active. */
void network_poll(void);
/* Process one frame before the caller reuses its receive buffer. */
void network_receive(const void *frame, size_t length);
/* Update link state. Link loss clears the current IPv4 lease. */
void network_set_link(const uint8_t mac[6], bool link_ready);
/* Copy the current link and IPv4 state when status is not null. */
void network_get_status(struct network_status *status);

#endif
