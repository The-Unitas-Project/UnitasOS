#include <kern/io.h>
#include <kern/interrupts.h>
#include <kern/ring_buffer.h>
#include <kern/serial.h>

#ifndef UNITAS_SERIAL
#define UNITAS_SERIAL 0
#endif

#if UNITAS_SERIAL
#define COM1 0x3f8
#define SERIAL_RX_CAPACITY 256

static uint8_t receive_storage[SERIAL_RX_CAPACITY];
static struct byte_ring receive_queue;

static uintptr_t irq_save(void) {
    uintptr_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uintptr_t flags) {
    if (flags & (1u << 9)) __asm__ volatile("sti" : : : "memory");
}

void serial_init(void) {
    byte_ring_init(&receive_queue, receive_storage, sizeof(receive_storage));
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc1);
    outb(COM1 + 4, 0x0b);
}

static void serial_irq(struct interrupt_frame *frame) {
    (void)frame;
    for (unsigned handled = 0; handled < 128; ++handled) {
        uint8_t interrupt_id = inb(COM1 + 2);
        if (interrupt_id & 0x01) return;
        switch (interrupt_id & 0x0e) {
        case 0x06:
            (void)inb(COM1 + 5);
            /* Drain bytes that arrived with a line error. */
        case 0x04:
        case 0x0c:
            while (inb(COM1 + 5) & 0x01)
                (void)byte_ring_push(&receive_queue, inb(COM1));
            break;
        case 0x00:
            (void)inb(COM1 + 6);
            break;
        default:
            return;
        }
    }
}

void serial_interrupts_init(void) {
    irq_register(4, serial_irq);
    pic_unmask(4);
    outb(COM1 + 1, 0x01);
}

void serial_putc(char character) {
    for (unsigned spin = 0; spin < 100000; ++spin)
        if (inb(COM1 + 5) & 0x20) break;
    outb(COM1, (uint8_t)character);
}

bool serial_read_char(char *out) {
    if (!out) return false;
    uintptr_t flags = irq_save();
    uint8_t value;
    bool available = byte_ring_pop(&receive_queue, &value);
    if (available) *out = (char)value;
    irq_restore(flags);
    return available;
}
#else
void serial_init(void) {}
void serial_interrupts_init(void) {}

void serial_putc(char character) {
    (void)character;
}

bool serial_read_char(char *out) {
    (void)out;
    return false;
}
#endif
