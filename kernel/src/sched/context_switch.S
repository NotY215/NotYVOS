/* void notyvos_switch_context(u64* save_slot, u64 new_rsp)
 *   rdi = &prev_task->kernel_rsp
 *   rsi =  next_task->kernel_rsp
 *
 * Saves callee-saved registers and RFLAGS on the current stack, writes RSP
 * to *save_slot, loads RSP from new_rsp, restores the same set, and returns.
 *
 * The incoming stack's layout must match the pops below, from low address
 * to high:
 *   [rsp+0]  rflags
 *   [rsp+8]  r15
 *   [rsp+16] r14
 *   [rsp+24] r13
 *   [rsp+32] r12
 *   [rsp+40] rbx
 *   [rsp+48] rbp
 *   [rsp+56] return target
 *
 * task_create_kernel/user build that layout in build_initial_stack().
 */

.intel_syntax noprefix
.section .text
.global notyvos_switch_context
.type notyvos_switch_context, @function
notyvos_switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi], rsp
    mov rsp, rsi

    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
.size notyvos_switch_context, .-notyvos_switch_context
