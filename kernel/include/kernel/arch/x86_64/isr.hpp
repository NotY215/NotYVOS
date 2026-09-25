#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

// Stack layout at the point isr_common calls notyvos_isr_dispatch.
// Field order MUST match the push order in isr_stubs.S.
//
// rsp and ss are only meaningful when (cs & 3) == 3 (interrupt from ring 3).
struct InterruptFrame
{
    u64 es;
    u64 ds;
    u64 r15, r14, r13, r12;
    u64 r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

extern "C" void notyvos_isr_dispatch(InterruptFrame* frame) noexcept;

// 48 real stubs (exceptions 0..31, PIC IRQs 0..15 mapped to vectors 32..47)
// plus one unhandled stub for vectors 48..255.
extern "C" uptr isr_stub_table[48];
extern "C" void isr_unhandled();

void interrupts_enable() noexcept;
void interrupts_disable() noexcept;

inline void interrupts_enable() noexcept
{
    asm volatile("sti" ::: "memory");
}
inline void interrupts_disable() noexcept
{
    asm volatile("cli" ::: "memory");
}

} // namespace notyvos::arch::x86_64
