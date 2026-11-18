/* layout de aplicativo: imagem contigua a partir de 0, bss depois */
ENTRY(nxe_main)
SECTIONS {
    . = 0;
    .image : {
        *(.text.nxe_main) *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(16);
    }
    .bss : { *(.bss .bss.*) *(COMMON) . = ALIGN(16); __nx_end = .; }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
