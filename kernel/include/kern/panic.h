#ifndef UNITAS_KERN_PANIC_H
#define UNITAS_KERN_PANIC_H

__attribute__((noreturn)) void panic(const char *message);
__attribute__((noreturn)) void panicf(const char *format, ...);

#endif
