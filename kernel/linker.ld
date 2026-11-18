OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS {
    . = 0x100000;
    .text : { *(.text.entry) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    .initrd : { __initrd_start = .; KEEP(*(.initrd)) __initrd_end = .; }
    . = ALIGN(4096);
    .bss : { __bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(16); __bss_end = .; }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
