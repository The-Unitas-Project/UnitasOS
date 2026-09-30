/* Link the kernel low because the bootstrap maps its addresses 1:1. */
ENTRY(_start)
PHDRS
{
    multiboot PT_LOAD FLAGS(4);
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}
SECTIONS
{
    . = 1M;
    __kernel_start = .;
    __multiboot_start = .;
    .multiboot ALIGN(8) : { KEEP(*(.multiboot)) } :multiboot
    . = ALIGN(4K);
    __multiboot_end = .;
    .text ALIGN(4K) : {
        __text_start = .;
        *(.text .text.*)
        . = ALIGN(4K);
        __text_end = .;
    } :text
    .rodata ALIGN(4K) : {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4K);
        __rodata_end = .;
    } :rodata
    .data ALIGN(4K) : {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4K);
        __data_end = .;
    } :data
    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4K);
        __bss_end = .;
    } :data
    . = ALIGN(4K);
    __kernel_end = .;
    /DISCARD/ : { *(.eh_frame .note .comment) }
}
