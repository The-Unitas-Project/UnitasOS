#include <kern/io.h>
#include <kern/serial.h>

#define COM1 0x3f8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

void serial_putc(char character) {
    for (unsigned spin = 0; spin < 100000; ++spin)
        if (inb(COM1 + 5) & 0x20) break;
    outb(COM1, (uint8_t)character);
}

bool serial_read_char(char *out) {
    if (!out || !(inb(COM1 + 5) & 1)) return false;
    *out = (char)inb(COM1);
    return true;
}
