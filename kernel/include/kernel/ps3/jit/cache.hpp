#pragma once
#include <kernel/ps3/jit/block.hpp>

namespace notyvos::ps3::jit
{

class TranslationCache
{
public:
    static void init() noexcept;

    static Block* lookup(u64 ppc_pc) noexcept;
    static void insert(Block* blk) noexcept;
    static void flush() noexcept;

    static u64 hits() noexcept;
    static u64 misses() noexcept;
    static u64 block_count() noexcept;
    static u64 bytes_used() noexcept;
};

} // namespace notyvos::ps3::jit
