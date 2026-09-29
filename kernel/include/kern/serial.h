#ifndef UNITAS_KERN_SERIAL_H
#define UNITAS_KERN_SERIAL_H

#include <stdbool.h>

void serial_init(void);
void serial_putc(char character);
bool serial_read_char(char *out);

#endif
