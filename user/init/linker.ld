/* NOTYVOS user-space linker script.
 *
 * Non-PIE. Fixed load address 0x400000. Every output section is page-aligned
 * so the kernel's segment loader (which maps whole 4 KiB pages) can never
 * leave holes between logically adjacent sections. */

ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4);   /* R     */
    data   PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
    . = 0x400000;

    .text : ALIGN(0x1000) {
        *(.text .text.*)
        *(.init .init.*)
        *(.fini .fini.*)
    } :text

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata .rodata.*)
        *(.rodata.cst*)
        *(.rodata.str*)
    } :rodata

    . = ALIGN(0x1000);
    .data : {
        *(.data .data.*)
        *(.data.rel.ro .data.rel.ro.*)
    } :data

    . = ALIGN(0x1000);
    .got : {
        *(.got .got.*)
        *(.got.plt .got.plt.*)
    } :data

    . = ALIGN(0x1000);
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(0x1000);
    } :data

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
