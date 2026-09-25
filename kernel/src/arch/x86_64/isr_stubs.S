/* NOTYVOS ISR stubs.
 *
 * 48 stubs (exceptions 0..31, PIC IRQs at vectors 32..47) plus one unhandled
 * stub for vectors 48..255.
 *
 * Every stub normalizes the stack to:
 *   [vector][error_code][CPU frame...]
 * where the CPU frame is what the CPU pushed automatically:
 *   [rip][cs][rflags][rsp][ss]  (rsp and ss present only on ring crossing)
 *
 * isr_common saves the general-purpose registers and both data segment
 * registers, calls notyvos_isr_dispatch, then unwinds and iretq's. */

.intel_syntax noprefix
.section .text

/* ---- Stubs without a CPU-pushed error code: push a dummy 0. ---- */
.macro ISR_NO_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    push 0
    push \num
    jmp isr_common
.size isr\num, .-isr\num
.endm

/* ---- Stubs with a CPU-pushed error code: only push the vector. ---- */
.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    push \num
    jmp isr_common
.size isr\num, .-isr\num
.endm

/* ---- Exceptions 0..31 ---- */
ISR_NO_ERR 0    /* #DE divide error */
ISR_NO_ERR 1    /* #DB debug */
ISR_NO_ERR 2    /* NMI */
ISR_NO_ERR 3    /* #BP breakpoint */
ISR_NO_ERR 4    /* #OF overflow */
ISR_NO_ERR 5    /* #BR bound range */
ISR_NO_ERR 6    /* #UD invalid opcode */
ISR_NO_ERR 7    /* #NM device not available */
ISR_ERR    8    /* #DF double fault */
ISR_NO_ERR 9    /* coprocessor segment overrun */
ISR_ERR    10   /* #TS invalid TSS */
ISR_ERR    11   /* #NP segment not present */
ISR_ERR    12   /* #SS stack-segment fault */
ISR_ERR    13   /* #GP general protection */
ISR_ERR    14   /* #PF page fault */
ISR_NO_ERR 15   /* reserved */
ISR_NO_ERR 16   /* #MF x87 FP */
ISR_ERR    17   /* #AC alignment check */
ISR_NO_ERR 18   /* #MC machine check */
ISR_NO_ERR 19   /* #XM SIMD FP */
ISR_NO_ERR 20   /* #VE virtualization */
ISR_ERR    21   /* #CP control protection */
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_ERR    29
ISR_ERR    30
ISR_NO_ERR 31

/* ---- PIC IRQs 0..15 mapped to vectors 32..47 ---- */
ISR_NO_ERR 32
ISR_NO_ERR 33
ISR_NO_ERR 34
ISR_NO_ERR 35
ISR_NO_ERR 36
ISR_NO_ERR 37
ISR_NO_ERR 38
ISR_NO_ERR 39
ISR_NO_ERR 40
ISR_NO_ERR 41
ISR_NO_ERR 42
ISR_NO_ERR 43
ISR_NO_ERR 44
ISR_NO_ERR 45
ISR_NO_ERR 46
ISR_NO_ERR 47

/* ---- Fallback for vectors 48..255 ---- */
.global isr_unhandled
.type isr_unhandled, @function
isr_unhandled:
    push 0
    push 48                 /* sentinel; handler reports "unhandled" */
    jmp isr_common
.size isr_unhandled, .-isr_unhandled

/* ---- Common entry ---- */
isr_common:
    /* Save general-purpose registers. */
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    /* Save segment registers (ds, es) via rax scratch. */
    xor rax, rax
    mov ax, ds
    push rax
    xor rax, rax
    mov ax, es
    push rax

    /* Load kernel data segment into ds/es. */
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    /* Call C dispatcher with rdi = pointer to InterruptFrame. */
    mov rdi, rsp
    cld
    call notyvos_isr_dispatch

    /* Restore segments. */
    pop rax
    mov es, ax
    pop rax
    mov ds, ax

    /* Restore general-purpose registers. */
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    /* Drop the vector and error_code we pushed. */
    add rsp, 16

    iretq

/* ---- Stub pointer table for idt_init ---- */
.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
    .quad isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    .quad isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .quad isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    .quad isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
