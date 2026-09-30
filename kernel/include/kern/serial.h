#ifndef UNITAS_KERN_SERIAL_H
#define UNITAS_KERN_SERIAL_H

#include <stdbool.h>

/* Serial input and /dev/serial0 require a build with serial=1. */
void serial_init(void);
void serial_interrupts_init(void);
void serial_putc(char character);
bool serial_read_char(char *out);

#endif
