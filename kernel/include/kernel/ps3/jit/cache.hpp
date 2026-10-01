#pragma once
#include <kernel/ps3/jit/block.hpp>

namespace notyvos::ps3::jit
{

// Fixed-bucket chained hash table mapping a PPC program counter to a
// translated Block. Single-threaded in Phase 5; the SMP phase will add
// per-CPU locks or partitioning.
class TranslationCache
{
public:
    static void init() noexcept;

    // Counted lookup: increments hits on success, misses on failure.
    static Block* lookup(u64 ppc_pc) noexcept;

    // Non-counted lookup.
    static Block* probe(u64 ppc_pc) noexcept;

    static void insert(Block* blk) noexcept;

    // Drop every block.
    static void flush() noexcept;

    // Drop every block whose ppc_start lies in [lo, hi).
    static void invalidate_range(u64 lo, u64 hi) noexcept;

    static u64 hits() noexcept;
    static u64 misses() noexcept;
    static u64 block_count() noexcept;
    static u64 bytes_used() noexcept;
};

} // namespace notyvos::ps3::jit
