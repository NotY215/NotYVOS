#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

inline void outb(u16 port, u8 value) noexcept
{
    asm volatile("outb %0, %1" ::"a"(value), "Nd"(port) : "memory");
}
inline void outw(u16 port, u16 value) noexcept
{
    asm volatile("outw %0, %1" ::"a"(value), "Nd"(port) : "memory");
}
inline void outl(u16 port, u32 value) noexcept
{
    asm volatile("outl %0, %1" ::"a"(value), "Nd"(port) : "memory");
}

inline u8 inb(u16 port) noexcept
{
    u8 v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
inline u16 inw(u16 port) noexcept
{
    u16 v;
    asm volatile("inw %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
inline u32 inl(u16 port) noexcept
{
    u32 v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

inline void io_wait() noexcept
{
    // Write to unused port 0x80 to burn ~1us on most chipsets.
    asm volatile("outb %%al, $0x80" ::"a"(0) : "memory");
}

} // namespace notyvos::arch::x86_64
