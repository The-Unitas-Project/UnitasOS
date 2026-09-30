#ifndef UNITAS_KERN_CONSOLE_H
#define UNITAS_KERN_CONSOLE_H

#include <stddef.h>

/* Select the VGA text console as the kernel output device. */
void console_init(void);
/* Write length bytes. The input does not need a trailing NUL byte. */
void console_write(const char *text, size_t length);
void console_putc(char character);
void console_clear(void);

#endif
