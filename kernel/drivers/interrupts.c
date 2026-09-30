#include <kern/interrupts.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/panic.h>
#include <kern/types.h>
#include <kern/user.h>

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
extern void (*isr_stub_table[48])(void);
extern void isr_default(void);
static struct idt_gate idt[256];
static irq_handler_t irq_handlers[16];

static void set_gate(unsigned vector, void (*entry)(void)) {
    uint64_t address = (uintptr_t)entry;
    idt[vector] = (struct idt_gate) {
        .offset_low = (uint16_t)address, .selector = 0x08, .ist = 0,
        .attributes = 0x8e, .offset_middle = (uint16_t)(address >> 16),
        .offset_high = (uint32_t)(address >> 32), .reserved = 0
    };
}

void interrupts_init(void) {
    for (unsigned i = 0; i < 256; ++i) set_gate(i, isr_default);
    for (unsigned i = 0; i < 48; ++i) set_gate(i, isr_stub_table[i]);
    struct idt_pointer pointer = { sizeof(idt) - 1, (uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(pointer));
}

void interrupts_register_user_call(void (*entry)(void)) {
    if (!entry) return;
    uint64_t address = (uintptr_t)entry;
    idt[0x81] = (struct idt_gate) {
        .offset_low = (uint16_t)address, .selector = 0x08, .ist = 0,
        .attributes = 0xee, .offset_middle = (uint16_t)(address >> 16),
        .offset_high = (uint32_t)(address >> 32), .reserved = 0
    };
}

void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq < ARRAY_SIZE(irq_handlers)) irq_handlers[irq] = handler;
}

int interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame->vector < 32) {
        if ((frame->cs & 3) == 3 &&
            user_handle_exception(frame->vector, frame->rip)) return 1;
        panicf("CPU exception vector=%llu error=0x%llx rip=0x%llx",
               (unsigned long long)frame->vector,
               (unsigned long long)frame->error_code,
               (unsigned long long)frame->rip);
    }
    if (frame->vector >= 32 && frame->vector < 48) {
        uint8_t irq = (uint8_t)(frame->vector - 32);
        if (irq_handlers[irq]) irq_handlers[irq](frame);
        pic_eoi(irq);
    }
    return 0;
}
