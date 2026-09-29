#ifndef UNITAS_KERN_CONSOLE_H
#define UNITAS_KERN_CONSOLE_H

#include <stddef.h>

void console_init(void);
void console_write(const char *text, size_t length);
void console_putc(char character);
void console_clear(void);

#endif
