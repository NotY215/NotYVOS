#pragma once
#include <kernel/types.hpp>

namespace notyvos::syscall
{

struct SyscallFrame
{
    u64 rax, r9, r8, r10, rdx, rsi, rdi, rbx, rbp, r12, r13, r14, r15;
    u64 rip, cs, rflags, rsp, ss;
};

void syscall_init() noexcept;

extern "C" void syscall_entry();
extern "C" void syscall_dispatch(SyscallFrame* frame) noexcept;

namespace nr
{
constexpr u64 kExit = 0;
constexpr u64 kWrite = 1;
constexpr u64 kYield = 2;
constexpr u64 kGetPid = 3;
} // namespace nr

} // namespace notyvos::syscall
