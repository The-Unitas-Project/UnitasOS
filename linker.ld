/* Link the kernel low because the bootstrap maps its addresses 1:1. */
ENTRY(_start)
SECTIONS
{
    . = 1M;
    __kernel_start = .;
    .multiboot ALIGN(8) : { KEEP(*(.multiboot)) }
    .text ALIGN(4K) : { *(.text .text.*) }
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }
    .data ALIGN(4K) : { *(.data .data.*) }
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }
    . = ALIGN(4K);
    __kernel_end = .;
    /DISCARD/ : { *(.eh_frame .note .comment) }
}
