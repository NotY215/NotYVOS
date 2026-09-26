/* NOTYVOS user-space linker script.
 *
 * Non-PIE. Fixed load address 0x400000. All sections accounted for;
 * everything unused is discarded so the linker cannot place stray
 * sections at unexpected addresses. */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : ALIGN(0x1000) {
        *(.text .text.*)
        *(.init .init.*)
        *(.fini .fini.*)
    }

    .rodata : ALIGN(0x1000) {
        *(.rodata .rodata.*)
        *(.rodata.cst*)
        *(.rodata.str*)
    }

    .data : ALIGN(0x1000) {
        *(.data .data.*)
        *(.data.rel.ro .data.rel.ro.*)
    }

    .bss : ALIGN(0x1000) {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
    }

    .got : ALIGN(0x1000) {
        *(.got .got.*)
        *(.got.plt .got.plt.*)
    }

    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr)
        *(.note .note.*)
        *(.comment)
        *(.interp)
        *(.dynamic)
        *(.dynsym .dynstr)
        *(.gnu.hash .hash)
        *(.rela .rela.* .rel .rel.*)
        *(.plt .plt.*)
        *(.init_array .fini_array .ctors .dtors)
        *(.tbss .tdata)
        *(.debug*)
    }
}
