#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

namespace gdt_sel
{
constexpr u16 kKernelCode = 0x08;
constexpr u16 kKernelData = 0x10;
constexpr u16 kUserData = 0x18;
constexpr u16 kUserCode = 0x20;
constexpr u16 kTss = 0x28;
} // namespace gdt_sel

struct GdtEntry
{
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 flags_limit_high;
    u8 base_high;
} __attribute__((packed));

struct GdtPointer
{
    u16 limit;
    u64 base;
} __attribute__((packed));

// 64-bit TSS descriptor occupies two adjacent GDT slots.
struct GdtTssDescriptor
{
    GdtEntry low;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

void gdt_init() noexcept;

} // namespace notyvos::arch::x86_64
