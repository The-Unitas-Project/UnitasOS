#ifndef UNITAS_KERN_INTERRUPTS_H
#define UNITAS_KERN_INTERRUPTS_H

#include <stdint.h>

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags;
} __attribute__((packed));

typedef void (*irq_handler_t)(struct interrupt_frame *frame);
void interrupts_init(void);
void irq_register(uint8_t irq, irq_handler_t handler);
void pic_unmask(uint8_t irq);
void interrupt_dispatch(struct interrupt_frame *frame);
void pic_init(void);
void pic_eoi(uint8_t irq);

#endif
