/* NOTYVOS kernel entry — Limine protocol v3, higher-half.
 * Limine enters us in long mode with paging on.
 * Interrupts are disabled, direction flag is clear.
 * rdi = pointer to limine boot info (struct limine_internal_entry's
 *       base_revision is available; the actual boot info passed is
 *       in the responses we registered).
 *
 * We register all Limine requests statically in C++ (see main.cpp),
 * and Limine writes back the responses there. So the entry point
 * does not need to consume any argument beyond calling into C++.
 */

.section .text
.global _start
.type _start, @function
_start:
    cli
    cld

    /* Ensure stack is 16-byte aligned before calling C++ */
    movq %rsp, %rbp
    andq $-16, %rsp

    /* Zero the frame pointer for clean stack traces */
    xorq %rbp, %rbp

    /* Call kernel_main() */
    call kernel_main

    /* kernel_main is [[noreturn]]; if it returns, halt forever. */
.halt:
    cli
    hlt
    jmp .halt
.size _start, .-_start