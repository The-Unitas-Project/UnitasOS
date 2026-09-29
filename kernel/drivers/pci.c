#include <kern/pci.h>
#include <kern/io.h>

#define PCI_CONFIG_ADDRESS 0x0cf8
#define PCI_CONFIG_DATA 0x0cfc

static uint32_t config_address(const struct pci_device *device, uint8_t offset) {
    return 0x80000000u | ((uint32_t)device->bus << 16) |
           ((uint32_t)device->device << 11) |
           ((uint32_t)device->function << 8) | (offset & 0xfcu);
}

uint32_t pci_read_config(const struct pci_device *device, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, config_address(device, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(const struct pci_device *device, uint8_t offset,
                      uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, config_address(device, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_enable_command(const struct pci_device *device, uint16_t bits) {
    uint32_t command_status = pci_read_config(device, 0x04);
    uint16_t command = (uint16_t)command_status | bits;
    pci_write_config(device, 0x04, command);
}

uint64_t pci_bar_address(const struct pci_device *device, unsigned index,
                         bool *is_io, bool *is_64_bit) {
    if (!device || index >= 6) return 0;
    uint32_t low = pci_read_config(device, (uint8_t)(0x10 + index * 4));
    if (low == 0 || low == 0xffffffffu) return 0;
    bool io = (low & 1) != 0;
    bool wide = !io && ((low >> 1) & 3) == 2 && index < 5;
    if (is_io) *is_io = io;
    if (is_64_bit) *is_64_bit = wide;
    uint64_t address = io ? (low & ~3u) : (low & ~0x0fu);
    if (wide) {
        uint32_t high = pci_read_config(device, (uint8_t)(0x14 + index * 4));
        address |= (uint64_t)high << 32;
    }
    return address;
}

static bool read_device(uint8_t bus, uint8_t slot, uint8_t function,
                        struct pci_device *device) {
    struct pci_device address = { .bus = bus, .device = slot, .function = function };
    uint32_t id = pci_read_config(&address, 0x00);
    if ((uint16_t)id == 0xffff) return false;
    uint32_t class_register = pci_read_config(&address, 0x08);
    uint32_t header_register = pci_read_config(&address, 0x0c);
    device->bus = bus;
    device->device = slot;
    device->function = function;
    device->vendor_id = (uint16_t)id;
    device->device_id = (uint16_t)(id >> 16);
    device->class_code = (uint8_t)(class_register >> 24);
    device->subclass = (uint8_t)(class_register >> 16);
    device->programming_interface = (uint8_t)(class_register >> 8);
    device->header_type = (uint8_t)(header_register >> 16);
    return true;
}

size_t pci_enumerate(pci_visit_fn visit, void *context) {
    size_t count = 0;
    for (unsigned bus = 0; bus < 256; ++bus) {
        for (unsigned slot = 0; slot < 32; ++slot) {
            struct pci_device device;
            if (!read_device((uint8_t)bus, (uint8_t)slot, 0, &device)) continue;
            if (visit) visit(&device, context);
            ++count;
            if (!(device.header_type & 0x80)) continue;
            for (unsigned function = 1; function < 8; ++function) {
                if (!read_device((uint8_t)bus, (uint8_t)slot,
                                 (uint8_t)function, &device)) continue;
                if (visit) visit(&device, context);
                ++count;
            }
        }
    }
    return count;
}
