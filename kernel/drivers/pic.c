#include <kern/interrupts.h>
#include <kern/io.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA 0xa1

void pic_init(void) {
    outb(PIC1_COMMAND, 0x11); io_wait();
    outb(PIC2_COMMAND, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xff); outb(PIC2_DATA, 0xff);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t line = irq < 8 ? irq : (uint8_t)(irq - 8);
    outb(port, (uint8_t)(inb(port) & ~(1u << line)));
    if (irq >= 8) outb(PIC1_DATA, (uint8_t)(inb(PIC1_DATA) & ~(1u << 2)));
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}
