#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

struct Tss
{
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

static_assert(sizeof(Tss) == 104, "TSS must be exactly 104 bytes");

struct TssDescriptorInfo
{
    u64 base;
    u32 limit;
};

TssDescriptorInfo tss_descriptor() noexcept;

void tss_init() noexcept;
void tss_set_rsp0(u64 rsp0) noexcept;

// IST index 1..7. rsp_top is the *highest* usable address of the stack.
void tss_set_ist(u32 index, u64 rsp_top) noexcept;

} // namespace notyvos::arch::x86_64
