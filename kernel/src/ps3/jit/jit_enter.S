/* void notyvos_jit_enter(void* ctx, void* code)
 *   rdi = Context* ctx
 *   rsi = entry point (ExecArena VA)
 *
 * Sets r15 = ctx for the JIT'd code (r15 is callee-saved, so the ABI
 * guarantees translated code may use it as a persistent base without
 * saving or restoring it), calls the entry point, then restores r15.
 * The JIT'd code only writes ctx->gpr[]/pc/lr/ctr/xer and RETs.
 */

.intel_syntax noprefix
.section .text
.global notyvos_jit_enter
.type notyvos_jit_enter, @function
notyvos_jit_enter:
    push r15
    mov r15, rdi
    call rsi
    pop r15
    ret
.size notyvos_jit_enter, .-notyvos_jit_enter
