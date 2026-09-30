.section .text.start, "ax"
.global _start
.type _start, @function
_start:
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    andq $-16, %rsp
    call main
    movl %eax, %edi
    call exit
1:
    jmp 1b

.section .note.GNU-stack,"",@progbits
