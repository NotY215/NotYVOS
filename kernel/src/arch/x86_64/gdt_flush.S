/* GDT/TSS reload helpers.
 *
 * LLVM's integrated assembler does not accept `lretq` in .intel_syntax.
 * We use the raw opcode 48 CB (REX.W + RETF) for the 64-bit far return,
 * which is unambiguous and portable across LLVM versions.
 */

.intel_syntax noprefix

.section .text

/* void notyvos_gdt_flush(const GdtPointer* ptr)
 *   rdi = pointer to { u16 limit; u64 base; } packed structure
 * Reloads GDTR and all segment registers, then far-returns to reload CS
 * with the kernel code selector 0x08. */
.global notyvos_gdt_flush
.type notyvos_gdt_flush, @function
notyvos_gdt_flush:
    cli
    lgdt [rdi]

    mov ax, 0x10            /* kernel data selector */
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Far return to reload CS with selector 0x08.
     * Stack layout for 64-bit far return: [RIP][CS] pushed in that order,
     * then RETF pops RIP and CS. */
    push 0x08
    lea rax, [rip + .Lreload_cs]
    push rax
    .byte 0x48, 0xCB        /* REX.W + RETF = 64-bit far return */
.Lreload_cs:
    ret
.size notyvos_gdt_flush, .-notyvos_gdt_flush

/* void notyvos_tss_load(u16 selector)
 *   di = TSS selector (0x28) */
.global notyvos_tss_load
.type notyvos_tss_load, @function
notyvos_tss_load:
    ltr di
    ret
.size notyvos_tss_load, .-notyvos_tss_load
.global notyvos_idt_flush
.type notyvos_idt_flush, @function
notyvos_idt_flush:
    lidt [rdi]
    ret
.size notyvos_idt_flush, .-notyvos_idt_flush
