#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

// Configure PIT channel 0 in mode 3 (square wave) at `hz`.
void pit_init(u32 hz) noexcept;

// Monotonic tick count since pit_init.
u64 pit_ticks() noexcept;

// Called from the IRQ0 handler. Increments tick counter, optionally logs.
void pit_on_tick() noexcept;

} // namespace notyvos::arch::x86_64
