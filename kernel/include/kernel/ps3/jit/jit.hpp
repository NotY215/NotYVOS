#pragma once
#include <kernel/ps3/ppu.hpp>

namespace notyvos::ps3::jit
{

void init() noexcept;

// Run the context through the JIT until `max_steps` PPC instructions have
// executed or the JIT cannot make progress. Falls back to ppu::step() for
// unsupported opcodes and for the syscall boundary.
//
// Returns the number of PPC instructions actually executed.
u64 run(ppu::Context* ctx, u64 max_steps) noexcept;

// Statistics.
u64 blocks_translated() noexcept;
u64 blocks_entered() noexcept;
u64 fallback_steps() noexcept;

} // namespace notyvos::ps3::jit
