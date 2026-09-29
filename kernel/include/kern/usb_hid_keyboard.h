#ifndef UNITAS_KERN_USB_HID_KEYBOARD_H
#define UNITAS_KERN_USB_HID_KEYBOARD_H

#include <stdint.h>

/* Reset report state before the host controller starts. */
void usb_hid_keyboard_init(void);
/* Decode one 8-byte boot report. Call this function in sequence from one context. */
void usb_hid_keyboard_report(const uint8_t report[8]);

#endif
