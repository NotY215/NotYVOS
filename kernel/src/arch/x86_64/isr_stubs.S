.intel_syntax noprefix
.section .text

.macro ISR_NO_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    push 0
    push \num
    jmp isr_common
.size isr\num, .-isr\num
.endm

.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    push \num
    jmp isr_common
.size isr\num, .-isr\num
.endm

ISR_NO_ERR 0
ISR_NO_ERR 1
ISR_NO_ERR 2
ISR_NO_ERR 3
ISR_NO_ERR 4
ISR_NO_ERR 5
ISR_NO_ERR 6
ISR_NO_ERR 7
ISR_ERR    8
ISR_NO_ERR 9
ISR_ERR    10
ISR_ERR    11
ISR_ERR    12
ISR_ERR    13
ISR_ERR    14
ISR_NO_ERR 15
ISR_NO_ERR 16
ISR_ERR    17
ISR_NO_ERR 18
ISR_NO_ERR 19
ISR_NO_ERR 20
ISR_ERR    21
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

.global isr_unhandled
.type isr_unhandled, @function
isr_unhandled:
    push 0
    push 48
    jmp isr_common
.size isr_unhandled, .-isr_unhandled

isr_common:
    /* If the interrupted context is user (CS low bits = 3), GS.base is
     * currently the user's and we must swapgs before touching GS. */
    test byte ptr [rsp + 160], 3
    jz 1f
    swapgs
1:

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

    xor rax, rax
    mov ax, ds
    push rax
    xor rax, rax
    mov ax, es
    push rax

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov rdi, rsp
    cld
    call notyvos_isr_dispatch

    pop rax
    mov es, ax
    pop rax
    mov ds, ax

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

    /* If returning to user, swapgs back. */
    test byte ptr [rsp + 24], 3
    jz 2f
    swapgs
2:
    add rsp, 16
    iretq

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
