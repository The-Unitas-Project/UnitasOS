#include <kern/console.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/serial.h>
#include <kern/types.h>
#include <stdint.h>

static const char *const level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

void log_init(void) { serial_init(); }

static void emit_char(char c) {
    console_putc(c);
}

static void emit_unsigned(uint64_t value, unsigned base, bool upper) {
    char buffer[32];
    size_t count = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { buffer[count++] = digits[value % base]; value /= base; } while (value);
    while (count) emit_char(buffer[--count]);
}

static void emit_format(const char *format, va_list args) {
    while (*format) {
        if (*format++ != '%') { emit_char(format[-1]); continue; }
        bool long_value = false, long_long_value = false;
        if (*format == 'l') { long_value = true; ++format; }
        if (long_value && *format == 'l') { long_long_value = true; ++format; }
        switch (*format ? *format++ : '\0') {
        case '%': emit_char('%'); break;
        case 'c': emit_char((char)va_arg(args, int)); break;
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            while (*s) emit_char(*s++);
            break;
        }
        case 'd': case 'i': {
            int64_t n = long_long_value ? va_arg(args, long long) :
                        long_value ? va_arg(args, long) : va_arg(args, int);
            if (n < 0) { emit_char('-'); emit_unsigned((uint64_t)(-(n + 1)) + 1, 10, false); }
            else emit_unsigned((uint64_t)n, 10, false);
            break;
        }
        case 'u': emit_unsigned(long_long_value ? va_arg(args, unsigned long long) :
                    long_value ? va_arg(args, unsigned long) : va_arg(args, unsigned), 10, false); break;
        case 'x': case 'X': emit_unsigned(long_long_value ? va_arg(args, unsigned long long) :
                    long_value ? va_arg(args, unsigned long) : va_arg(args, unsigned), 16, format[-1] == 'X'); break;
        case 'p': emit_unsigned((uintptr_t)va_arg(args, void *), 16, false); break;
        case '\0': return;
        default: emit_char('?'); break;
        }
    }
}

void log_vwrite(enum log_level level, const char *format, va_list args) {
    if ((unsigned)level < ARRAY_SIZE(level_names)) {
        emit_char('[');
        const char *name = level_names[level];
        while (*name) emit_char(*name++);
        emit_char(']'); emit_char(' ');
    }
    va_list copy;
    va_copy(copy, args);
    emit_format(format, copy);
    va_end(copy);
}

void log_write(enum log_level level, const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_vwrite(level, format, args);
    va_end(args);
}
