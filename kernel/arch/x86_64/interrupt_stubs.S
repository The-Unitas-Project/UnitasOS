/*
 * Each CPU vector uses the same register frame. The C handler reads this frame.
 * An exception with an error code pushes it. Other vectors push zero.
 */
.section .text, "ax"
.code64
.extern interrupt_dispatch
.extern user_return_to_kernel
.macro ISR_NOERR n
.global isr\n
isr\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm
.macro ISR_ERR n
.global isr\n
isr\n:
    pushq $\n
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

.global isr_default
isr_default:
    pushq $0
    pushq $255
    jmp isr_common

isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    /* Read CR2 before the C handler can cause another page fault. */
    movq %cr2, %rsi
    movq %rsp, %rbx
    andq $-16, %rsp
    call interrupt_dispatch
    testl %eax, %eax
    jnz user_return_to_kernel
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
    .quad isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7
    .quad isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15
    .quad isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23
    .quad isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
    .quad isr32,isr33,isr34,isr35,isr36,isr37,isr38,isr39
    .quad isr40,isr41,isr42,isr43,isr44,isr45,isr46,isr47
.section .note.GNU-stack,"",@progbits
