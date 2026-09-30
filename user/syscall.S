.section .text, "ax"
#include <unitas/syscall.h>
.global __unitas_syscall
.type __unitas_syscall, @function
__unitas_syscall:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    int $UNITAS_SYSCALL_VECTOR
    ret

/* Pass Linux syscall arguments in the x86-64 syscall register order. */
.global __linux_syscall
.type __linux_syscall, @function
__linux_syscall:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
    syscall
    ret

.section .note.GNU-stack,"",@progbits
