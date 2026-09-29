#include <kern/log.h>
#include <kern/pci.h>
#include <kern/usb.h>
#include <kern/usb_hid_keyboard.h>

struct usb_probe_result {
    size_t xhci_count;
    size_t legacy_count;
    size_t unknown_count;
};

static void inspect_pci_device(const struct pci_device *device, void *context) {
    if (device->class_code != 0x0c || device->subclass != 0x03) return;
    struct usb_probe_result *result = context;
    if (device->programming_interface == 0x30) {
        bool is_io = false;
        bool is_64_bit = false;
        uint64_t bar = pci_bar_address(device, 0, &is_io, &is_64_bit);
        ++result->xhci_count;
        if (!bar || is_io || bar >= 0x100000000ULL) {
            log_write(LOG_WARN,
                      "xHCI controller %x:%x has no usable MMIO BAR below 4 GiB\n",
                      (unsigned)device->vendor_id, (unsigned)device->device_id);
            return;
        }
        log_write(LOG_INFO,
                  "found xHCI controller %x:%x at MMIO 0x%llx (%s-bit BAR); transfers are not enabled\n",
                  (unsigned)device->vendor_id, (unsigned)device->device_id,
                  (unsigned long long)bar, is_64_bit ? "64" : "32");
        return;
    }
    if (device->programming_interface == 0x00 ||
        device->programming_interface == 0x10 ||
        device->programming_interface == 0x20) {
        ++result->legacy_count;
        log_write(LOG_INFO, "found legacy USB host controller %x:%x (not enabled)\n",
                  (unsigned)device->vendor_id, (unsigned)device->device_id);
        return;
    }
    ++result->unknown_count;
    log_write(LOG_INFO, "found unsupported USB host controller %x:%x interface 0x%x\n",
              (unsigned)device->vendor_id, (unsigned)device->device_id,
              (unsigned)device->programming_interface);
}

void usb_probe_controllers(void) {
    usb_hid_keyboard_init();
    struct usb_probe_result result = {0};
    (void)pci_enumerate(inspect_pci_device, &result);
    if (!result.xhci_count && !result.legacy_count && !result.unknown_count)
        log_write(LOG_INFO, "no PCI USB host controller found\n");
}
