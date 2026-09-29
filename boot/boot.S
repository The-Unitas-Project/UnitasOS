/*
 * Multiboot2 enters in 32-bit protected mode. This tiny trampoline creates
 * identity mappings for the first 4 GiB, enables PAE + long mode, and transfers
 * the boot information pointer to kernel_main according to the SysV ABI.
 */
.section .multiboot, "a"
.align 8
mb2_header:
    .long 0xe85250d6                 /* Multiboot2 magic */
    .long 0                           /* i386 architecture */
    .long mb2_header_end - mb2_header
    .long -(0xe85250d6 + (mb2_header_end - mb2_header))
    .short 0                          /* Required end tag */
    .short 0
    .long 8
mb2_header_end:

.section .bss, "aw", @nobits
.align 4096
boot_pml4: .skip 4096
boot_pdpt: .skip 4096
boot_pd0:  .skip 4096
boot_pd1:  .skip 4096
boot_pd2:  .skip 4096
boot_pd3:  .skip 4096
.align 16
boot_stack_bottom: .skip 16384
boot_stack_top:

.section .text, "ax"
.global _start
.type _start, @function
.code32
_start:
    cli
    movl $boot_stack_top, %esp
    movl %eax, %esi                  /* Preserve Multiboot2 magic */
    movl %ebx, %ebp                  /* Preserve information structure */

    /* Four page directories identity-map the full 4 GiB boot address space. */
    movl $boot_pdpt, %eax
    orl $0x3, %eax
    movl %eax, boot_pml4
    movl $0, boot_pml4 + 4
    movl $boot_pd0, %eax
    orl $0x3, %eax
    movl %eax, boot_pdpt
    movl $0, boot_pdpt + 4
    movl $boot_pd1, %eax
    orl $0x3, %eax
    movl %eax, boot_pdpt + 8
    movl $0, boot_pdpt + 12
    movl $boot_pd2, %eax
    orl $0x3, %eax
    movl %eax, boot_pdpt + 16
    movl $0, boot_pdpt + 20
    movl $boot_pd3, %eax
    orl $0x3, %eax
    movl %eax, boot_pdpt + 24
    movl $0, boot_pdpt + 28

    movl $boot_pd0, %edi
    /* The initial identity map is writable and executable across all 4 GiB. */
    movl $0x83, %eax                  /* Present, writable, 2 MiB page */
    movl $2048, %ecx
1:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    movl %cr4, %eax
    orl $(1 << 5), %eax               /* PAE */
    movl %eax, %cr4
    movl $boot_pml4, %eax
    movl %eax, %cr3

    movl $0xc0000080, %ecx            /* EFER MSR */
    rdmsr
    orl $(1 << 8), %eax                /* Long mode enable */
    wrmsr

    movl %cr0, %eax
    orl $(1 << 31), %eax               /* Paging */
    orl $(1 << 0), %eax                /* Protected mode */
    movl %eax, %cr0
    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movq $boot_stack_top, %rsp
    andq $-16, %rsp

    /* Initialize floating-point state before entering C code. */
    movq %cr0, %rax
    andq $-13, %rax                   /* Clear EM and TS */
    orq $0x22, %rax                   /* Set MP and NE */
    movq %rax, %cr0
    movq %cr4, %rax
    orq $0x600, %rax                  /* OSFXSR and OSXMMEXCPT */
    movq %rax, %cr4
    fninit
    ldmxcsr mxcsr_default(%rip)

    /* Pass (magic, info pointer). kernel_main validates the protocol magic. */
    movl %esi, %edi
    movl %ebp, %esi
    call kernel_main
2:  cli
    hlt
    jmp 2b

.align 8
gdt64:
    .quad 0
    .quad 0x00af9a000000ffff            /* 64-bit code */
    .quad 0x00af92000000ffff            /* data */
gdt64_end:
gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .long gdt64
.align 4
mxcsr_default:
    .long 0x1f80

.section .note.GNU-stack,"",@progbits
