#pragma once
#include <kernel/ps3/ppu.hpp>
#include <kernel/types.hpp>

namespace notyvos::ps3::abi
{

// ---------------------------------------------------------------------------
// PS3 syscall ABI — Phase 7B
//
// Numbers follow the CellOS convention. Syscall number is in GPR[3]
// (r3), arguments in GPR[4..10], return value written to GPR[3].
//
// The dispatcher is called by the PPU interpreter when it hits a `sc`
// instruction with the PS3 ABI installed. It returns true if the call
// was handled, false otherwise (which terminates the interpreter loop).
// ---------------------------------------------------------------------------

namespace nr
{
constexpr u64 kProcessExit      = 1;
constexpr u64 kProcessFork      = 2;
constexpr u64 kRead             = 3;
constexpr u64 kWrite            = 4;
constexpr u64 kOpen             = 5;
constexpr u64 kClose            = 6;
constexpr u64 kGetPid           = 20;
constexpr u64 kCellFsOpen       = 202;
constexpr u64 kCellFsRead       = 203;
constexpr u64 kCellFsWrite      = 204;
constexpr u64 kCellFsClose      = 205;
} // namespace nr

// Install the PS3 ABI into a PPU context. Sets ctx->syscall and ctx->user.
// The `void* user` passed to the dispatcher is the PPU context itself.
void install(ppu::Context* ctx) noexcept;

// The dispatcher. Signature matches ppu::Context::syscall.
bool dispatch(void* user, ppu::Context* ctx) noexcept;

// Stats.
u64 calls_handled() noexcept;
u64 calls_unknown() noexcept;
u64 process_exits() noexcept;

} // namespace notyvos::ps3::abi