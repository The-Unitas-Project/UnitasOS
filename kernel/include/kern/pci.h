#ifndef UNITAS_KERN_PCI_H
#define UNITAS_KERN_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t header_type;
};

typedef void (*pci_visit_fn)(const struct pci_device *device, void *context);
size_t pci_enumerate(pci_visit_fn visit, void *context);
uint32_t pci_read_config(const struct pci_device *device, uint8_t offset);
void pci_write_config(const struct pci_device *device, uint8_t offset,
                      uint32_t value);
void pci_enable_command(const struct pci_device *device, uint16_t bits);
uint64_t pci_bar_address(const struct pci_device *device, unsigned index,
                         bool *is_io, bool *is_64_bit);

#endif
