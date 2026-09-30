#ifndef UNITAS_KERN_KEYBOARD_H
#define UNITAS_KERN_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
void keyboard_init(void);
/* Return one queued character without waiting. */
bool keyboard_read_char(char *out);
/* Return false when the fixed-size input queue is full. */
bool keyboard_queue_char(char character);

#endif
