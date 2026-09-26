/* NOTYVOS syscall entry.
 *
 * On entry (SYSCALL instruction from ring 3):
 *   - CS = kernel code (0x08), SS = kernel data (0x10)
 *   - RIP = IA32_LSTAR (this function)
 *   - RCX = user RIP
 *   - R11 = user RFLAGS
 *   - RFLAGS has IA32_FMASK applied (IF cleared)
 *   - GS.base is the user's (0 in Phase 2A); KERNEL_GS_BASE = CpuLocal
 *   - RSP is still the user's
 *
 * We:
 *   1. swapgs so GS.base = kernel CpuLocal.
 *   2. Save user RSP into CpuLocal.syscall_user_rsp (offset 0).
 *   3. Load kernel RSP from CpuLocal.syscall_kernel_rsp (offset 8).
 *   4. Build iretq frame (SS, RSP, RFLAGS, CS, RIP) by pushing them.
 *   5. Push GPRs, with rax (the syscall number) pushed last so that
 *      [rsp + 0] is the frame's rax slot.
 *   6. Call syscall_dispatch(frame*).
 *   7. Pop GPRs. The pop of rax reads the value the dispatcher stored
 *      into f->rax. swapgs back. iretq.
 *
 * CpuLocal offsets (see percpu.hpp):
 *   0  syscall_user_rsp
 *   8  syscall_kernel_rsp
 */

.intel_syntax noprefix
.section .text
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    swapgs

    mov qword ptr gs:[0], rsp
    mov rsp, qword ptr gs:[8]

    /* Build iretq frame. Load user RSP with "push mem" so no register is
     * clobbered; rax must still hold the syscall number for step 5. */
    push 0x1B                       /* SS  = user data (0x18 | RPL 3) */
    push qword ptr gs:[0]           /* RSP = user stack pointer        */
    push r11                        /* RFLAGS = user RFLAGS            */
    push 0x23                       /* CS  = user code (0x20 | RPL 3)  */
    push rcx                        /* RIP = user return address       */

    /* Save GPRs. rax last, so [rsp] is the frame's rax slot. */
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rax

    mov rdi, rsp
    cld
    call syscall_dispatch

    /* No write-back needed. syscall_dispatch stores the return value into
     * f->rax (which is [rsp + 0] here). The next pop rax loads it. */

    pop rax
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    swapgs
    iretq
.size syscall_entry, .-syscall_entry
