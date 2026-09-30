.section .text, "ax"
.code64
.extern user_resume_rsp
.extern user_resume_rflags
.extern user_syscall_dispatch
.extern user_linux_syscall
.extern user_kernel_stack_top
.extern user_syscall_user_rsp
.extern user_syscall_saved_r12
.extern user_syscall_exit_requested
.extern user_syscall_exec_entry
.extern user_syscall_exec_stack
.extern user_syscall_exec_requested

.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, user_resume_rsp(%rip)
    pushfq
    popq %rax
    movq %rax, user_resume_rflags(%rip)
    pushq $0x1b
    pushq %rsi
    pushq $0x202
    pushq $0x23
    pushq %rdi
    iretq

.global user_syscall_entry
.type user_syscall_entry, @function
user_syscall_entry:
    cld
    /* SYSCALL keeps the user stack active, so save it before switching stacks. */
    movq %rsp, user_syscall_user_rsp(%rip)
    movq %r12, user_syscall_saved_r12(%rip)
    movq user_kernel_stack_top(%rip), %rsp
    andq $-16, %rsp
    subq $128, %rsp
    movq %rsp, %r12
    movq %rax, 0(%r12)
    movq %rdi, 8(%r12)
    movq %rsi, 16(%r12)
    movq %rdx, 24(%r12)
    movq %r10, 32(%r12)
    movq %r8, 40(%r12)
    movq %r9, 48(%r12)
    movq %rbx, 56(%r12)
    movq %rbp, 64(%r12)
    movq user_syscall_saved_r12(%rip), %rax
    movq %rax, 72(%r12)
    movq %r13, 80(%r12)
    movq %r14, 88(%r12)
    movq %r15, 96(%r12)
    movq %rcx, 104(%r12)
    movq %r11, 112(%r12)

    movq 0(%r12), %rdi
    movq 8(%r12), %rsi
    movq 16(%r12), %rdx
    movq 24(%r12), %rcx
    movq 32(%r12), %r8
    movq 40(%r12), %r9
    subq $16, %rsp
    movq 48(%r12), %rax
    movq %rax, (%rsp)
    call user_linux_syscall
    addq $16, %rsp

    cmpl $0, user_syscall_exit_requested(%rip)
    jne user_return_to_kernel
    cmpl $0, user_syscall_exec_requested(%rip)
    jne user_return_to_new_image
    movq 56(%r12), %rbx
    movq 64(%r12), %rbp
    movq 80(%r12), %r13
    movq 88(%r12), %r14
    movq 96(%r12), %r15
    movq 8(%r12), %rdi
    movq 16(%r12), %rsi
    movq 24(%r12), %rdx
    movq 32(%r12), %r10
    movq 40(%r12), %r8
    movq 48(%r12), %r9
    movq 104(%r12), %rcx
    movq 112(%r12), %r11
    movq 72(%r12), %r12
    movq user_syscall_user_rsp(%rip), %rsp
    sysretq

user_return_to_new_image:
    /* SYSRET enters the replacement image with a clean register state. */
    movl $0, user_syscall_exec_requested(%rip)
    movq user_syscall_exec_entry(%rip), %rcx
    movq user_syscall_exec_stack(%rip), %rsp
    movq $0x202, %r11
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rdx, %rdx
    xorq %rdi, %rdi
    xorq %rsi, %rsi
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    xorq %rbp, %rbp
    sysretq

.global user_interrupt_stub
.type user_interrupt_stub, @function
user_interrupt_stub:
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
    movq %rsp, %rbx
    andq $-16, %rsp
    movq %rbx, %rdi
    call user_syscall_dispatch
    movq %rbx, %rsp
    testl %eax, %eax
    jnz user_return_to_kernel
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
    iretq

.global user_return_to_kernel
user_return_to_kernel:
    movq user_resume_rsp(%rip), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    pushq user_resume_rflags(%rip)
    popfq
    ret

.section .note.GNU-stack,"",@progbits
