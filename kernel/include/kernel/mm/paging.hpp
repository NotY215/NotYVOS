#pragma once

#include <kernel/types.hpp>

namespace notyvos::mm
{

namespace page_flags
{
constexpr u64 Present = 1ULL << 0;
constexpr u64 Writable = 1ULL << 1;
constexpr u64 User = 1ULL << 2;
constexpr u64 PWT = 1ULL << 3;
constexpr u64 PCD = 1ULL << 4;
constexpr u64 Accessed = 1ULL << 5;
constexpr u64 Dirty = 1ULL << 6;
constexpr u64 Huge = 1ULL << 7;
constexpr u64 Global = 1ULL << 8;
constexpr u64 NX = 1ULL << 63;

constexpr u64 kPhysicalMask = 0x000FFFFFFFFFF000ULL;
constexpr u64 kFlagsMask = 0xFFF0000000000FFFULL;
} // namespace page_flags

constexpr u64 kPageSize = 0x1000ULL;
constexpr u64 kPageMask = 0xFFFFFFFFFFFFF000ULL;
constexpr u64 kPageOffset = 0x0000000000000FFFULL;

inline u32 pml4_index(uptr virt) noexcept
{
    return static_cast<u32>((virt >> 39) & 0x1FF);
}
inline u32 pdpt_index(uptr virt) noexcept
{
    return static_cast<u32>((virt >> 30) & 0x1FF);
}
inline u32 pd_index(uptr virt) noexcept
{
    return static_cast<u32>((virt >> 21) & 0x1FF);
}
inline u32 pt_index(uptr virt) noexcept
{
    return static_cast<u32>((virt >> 12) & 0x1FF);
}

constexpr usize kPageTableEntries = 512;

} // namespace notyvos::mm
