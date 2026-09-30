#ifndef UNITAS_KERN_LOG_H
#define UNITAS_KERN_LOG_H

#include <stdarg.h>

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
void log_init(void);
/* Format output with the kernel's small printf-style formatter. */
void log_write(enum log_level level, const char *format, ...);
void log_vwrite(enum log_level level, const char *format, va_list args);

#endif
