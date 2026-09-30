.section .text
.global _start
.type _start, @function
_start:
    /* Keep argv[0]. Coreutils uses it to select the command. */
    movq (%rsp), %rcx
    leaq 8(%rsp), %rsi
    leaq 16(%rsp,%rcx,8), %rdx
    leaq coreutils_path(%rip), %rdi
    movl $59, %eax
    syscall

    movl $127, %edi
    movl $60, %eax
    syscall
    ud2

.section .rodata
coreutils_path:
    .asciz "/bin/coreutils"

.section .note.GNU-stack,"",@progbits
