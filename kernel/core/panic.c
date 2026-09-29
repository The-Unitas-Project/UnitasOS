#include <kern/io.h>
#include <kern/log.h>
#include <kern/panic.h>

__attribute__((noreturn)) void panic(const char *message) {
    cpu_disable_interrupts();
    log_write(LOG_ERROR, "PANIC: %s\n", message);
    for (;;) cpu_halt();
}

__attribute__((noreturn)) void panicf(const char *format, ...) {
    __builtin_va_list args;
    cpu_disable_interrupts();
    log_write(LOG_ERROR, "PANIC: ");
    __builtin_va_start(args, format);
    log_vwrite(LOG_ERROR, format, args);
    __builtin_va_end(args);
    log_write(LOG_ERROR, "\n");
    for (;;) cpu_halt();
}
