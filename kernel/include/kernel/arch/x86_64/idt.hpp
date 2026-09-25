#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

struct IdtEntry
{
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed));

static_assert(sizeof(IdtEntry) == 16, "IDT entry must be 16 bytes");

struct IdtPointer
{
    u16 limit;
    u64 base;
} __attribute__((packed));

void idt_init() noexcept;
void idt_set_gate(u8 vector, uptr handler, u16 selector, u8 ist, u8 type_attr) noexcept;

} // namespace notyvos::arch::x86_64
