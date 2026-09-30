#ifndef UNITAS_KERN_ACPI_H
#define UNITAS_KERN_ACPI_H

#include <stddef.h>

/* Validate the Root System Description Pointer (RSDP) and cache x86 I/O controls. */
int acpi_init(const void *rsdp, size_t rsdp_size);
/* Return only if the firmware cannot complete the power transition. */
int acpi_poweroff(void);
/* Try the firmware reset port, then the legacy keyboard-controller reset. */
int acpi_reboot(void);

#endif
