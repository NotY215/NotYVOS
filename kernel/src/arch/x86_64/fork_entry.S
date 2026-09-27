/* void notyvos_enter_user_fork(const syscall::SyscallFrame* frame)
 *   rdi = pointer to SyscallFrame
 *
 * Restores the user register state from `frame`, sets rax=0 (fork returns 0
 * in the child), and iretqs to user mode.
 *
 * SyscallFrame layout (see syscall.hpp):
 *   0    rax
 *   8    r9
 *   16   r8
 *   24   r10
 *   32   rdx
 *   40   rsi
 *   48   rdi
 *   56   rbx
 *   64   rbp
 *   72   r12
 *   80   r13
 *   88   r14
 *   96   r15
 *   104  rip
 *   112  cs
 *   120  rflags
 *   128  rsp
 *   136  ss
 */

.intel_syntax noprefix
.section .text
.global notyvos_enter_user_fork
.type notyvos_enter_user_fork, @function
notyvos_enter_user_fork:
    swapgs

    /* Build the iretq frame: SS, RSP, RFLAGS, CS, RIP. */
    mov rax, [rdi + 136]
    push rax
    mov rax, [rdi + 128]
    push rax
    mov rax, [rdi + 120]
    push rax
    mov rax, [rdi + 112]
    push rax
    mov rax, [rdi + 104]
    push rax

    /* Restore GPRs. rdi must be restored last because it holds the frame
     * pointer we are still reading from. Save the frame pointer in r11. */
    mov r11, rdi

    mov r15, [r11 + 96]
    mov r14, [r11 + 88]
    mov r13, [r11 + 80]
    mov r12, [r11 + 72]
    mov rbp, [r11 + 64]
    mov rbx, [r11 + 56]
    mov rsi, [r11 + 40]
    mov rdx, [r11 + 32]
    mov r10, [r11 + 24]
    mov r8,  [r11 + 16]
    mov r9,  [r11 + 8]
    mov rdi, [r11 + 48]

    /* Child return value from fork(). */
    xor rax, rax
    iretq
.size notyvos_enter_user_fork, .-notyvos_enter_user_fork
