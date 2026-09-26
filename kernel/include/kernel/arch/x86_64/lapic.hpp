#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

class Lapic
{
public:
    // hhdm_offset = Limine's HHDM virtual base. Required so we can map
    // the LAPIC MMIO page into our address space ourselves; Limine's
    // HHDM does not map MMIO.
    static void init_bsp(u64 hhdm_offset) noexcept;
    static void enable_for_ap() noexcept;

    static u32 id() noexcept;
    static void send_eoi() noexcept;
    static bool is_present() noexcept;

private:
    static void map_mmio(u64 phys) noexcept;
};

} // namespace notyvos::arch::x86_64
