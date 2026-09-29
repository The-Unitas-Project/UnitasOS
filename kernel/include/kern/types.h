#ifndef UNITAS_KERN_TYPES_H
#define UNITAS_KERN_TYPES_H

/* Use fixed-width integers for hardware registers and on-disk fields. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;
typedef uint64_t pfn_t;

#define PAGE_SIZE 4096UL
#define PAGE_MASK (PAGE_SIZE - 1UL)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(v, a) ((v) & ~((a) - 1))

#endif
