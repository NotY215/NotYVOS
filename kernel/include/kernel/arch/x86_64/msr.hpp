#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

namespace msr
{
constexpr u32 kIa32Efer = 0xC0000080;
constexpr u32 kIa32Star = 0xC0000081;
constexpr u32 kIa32Lstar = 0xC0000082;
constexpr u32 kIa32Fmask = 0xC0000084;
constexpr u32 kIa32GsBase = 0xC0000101;
constexpr u32 kIa32KernelGsBase = 0xC0000102;

constexpr u64 kEferSce = 1ULL << 0;  // syscall/sysret enable
constexpr u64 kEferLme = 1ULL << 8;  // long mode enable
constexpr u64 kEferNxe = 1ULL << 11; // no-execute enable
} // namespace msr

inline u64 rdmsr(u32 msr_index) noexcept
{
    u32 lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr_index));
    return (static_cast<u64>(hi) << 32) | static_cast<u64>(lo);
}

inline void wrmsr(u32 msr_index, u64 value) noexcept
{
    const u32 lo = static_cast<u32>(value);
    const u32 hi = static_cast<u32>(value >> 32);
    asm volatile("wrmsr" ::"c"(msr_index), "a"(lo), "d"(hi) : "memory");
}

} // namespace notyvos::arch::x86_64
