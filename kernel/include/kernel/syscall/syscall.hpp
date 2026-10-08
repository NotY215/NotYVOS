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

// Set by syscall_dispatch at entry, cleared at exit. sys_exec rewrites it
// so the iretq lands at the new program's entry point.
extern SyscallFrame* g_current_frame;

namespace nr
{
constexpr u64 kGameRun = 18;
constexpr u64 kExit = 0;
constexpr u64 kWrite = 1;
constexpr u64 kYield = 2;
constexpr u64 kGetPid = 3;
constexpr u64 kOpen = 4;
constexpr u64 kRead = 5;
constexpr u64 kClose = 6;
constexpr u64 kFork = 7;
constexpr u64 kWait = 8;
constexpr u64 kReaddir = 9;
constexpr u64 kMmap = 10;
constexpr u64 kExec = 11;
constexpr u64 kBrk = 12;
constexpr u64 kTime = 13;
constexpr u64 kSleep = 14;
constexpr u64 kKill = 15;
constexpr u64 kCreate = 16;
constexpr u64 kUnlink = 17;
} // namespace nr

} // namespace notyvos::syscall
