#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

class Lapic
{
public:
    static void init_bsp() noexcept;
    static void enable_for_ap() noexcept;

    static u32 id() noexcept; // current CPU's LAPIC ID
    static void send_eoi() noexcept;
    static bool is_present() noexcept;
};

} // namespace notyvos::arch::x86_64
