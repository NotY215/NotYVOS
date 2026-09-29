#pragma once
#include <kernel/types.hpp>

namespace notyvos::mm
{
// Fixed-address executable arena. Backed by PMM frames, mapped into the
// kernel higher-half VA space with Present|Writable. W^X lands in a
// later phase; for Phase 4E the arena is RWX to keep the emitter simple.
class ExecArena
{
public:
    static void init() noexcept;
    // Copy `size` bytes of `code` into the next available slot and return
    // the VA. 16-byte aligned. Returns nullptr on exhaustion.
    static void* publish(const void* code, usize size) noexcept;
    static usize used_bytes() noexcept;
    static usize capacity_bytes() noexcept;
};

} // namespace notyvos::mm
