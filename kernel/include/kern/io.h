#ifndef UNITAS_KERN_IO_H
#define UNITAS_KERN_IO_H

#include <stdint.h>

/* Keep x86 port I/O instructions in this header. */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cpu_halt(void) { __asm__ volatile("hlt"); }
static inline void cpu_disable_interrupts(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void cpu_enable_interrupts(void) { __asm__ volatile("sti" ::: "memory"); }

#endif
