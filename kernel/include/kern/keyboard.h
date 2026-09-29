#ifndef UNITAS_KERN_KEYBOARD_H
#define UNITAS_KERN_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
void keyboard_init(void);
bool keyboard_read_char(char *out);
bool keyboard_queue_char(char character);

#endif
