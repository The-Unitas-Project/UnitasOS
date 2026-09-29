#include <kern/interrupts.h>
#include <kern/io.h>
#include <kern/timer.h>

#define PIT_INPUT_HZ 1193182u
static volatile uint64_t ticks;

static void timer_irq(struct interrupt_frame *frame) {
    (void)frame;
    ++ticks;
}

void pit_init(uint32_t frequency_hz) {
    if (!frequency_hz) frequency_hz = 100;
    if (frequency_hz > PIT_INPUT_HZ) frequency_hz = PIT_INPUT_HZ;
    uint32_t divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor > 65535) divisor = 65535;
    irq_register(0, timer_irq);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
    pic_unmask(0);
}

uint64_t timer_ticks(void) { return ticks; }

void timer_sleep(uint64_t duration) {
    uint64_t start = ticks;
    while (ticks - start < duration) __asm__ volatile("sti; hlt" ::: "memory");
}
