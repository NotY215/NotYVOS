#pragma once

#include <kernel/mm/paging.hpp>

namespace notyvos::mm
{

class VirtualMemory
{
public:
    static void init(u64 hhdm_offset) noexcept;

    // Map one page. `virt` and `phys` are page-aligned.
    static bool map_page(uptr virt, uptr phys, u64 flags) noexcept;

    // Remove one page mapping. Returns true if it was present.
    static bool unmap_page(uptr virt) noexcept;

    // Physical address backing `virt`, or 0 if not mapped.
    static uptr get_physical(uptr virt) noexcept;

    static void flush_tlb_page(uptr virt) noexcept;
    static void flush_tlb_all() noexcept;

    static uptr kernel_pml4_physical() noexcept;
};

} // namespace notyvos::mm
