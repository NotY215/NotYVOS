#pragma once

#include <kernel/types.hpp>
#include <limine.h>

namespace notyvos::mm
{

class PhysicalMemory
{
public:
    static void init(const limine_memmap_response* memmap, u64 hhdm_offset) noexcept;

    // Returns a 4 KB-aligned physical address, or 0 on failure.
    static uptr allocate_frame() noexcept;

    // `phys` must be a value returned by allocate_frame().
    static void free_frame(uptr phys) noexcept;

    // Contiguous run of `count` frames. Returns base physical address or 0.
    static uptr allocate_contiguous(usize count) noexcept;

    static u64 total_bytes() noexcept;
    static u64 free_bytes() noexcept;
    static u64 used_bytes() noexcept;
    static u64 total_frames() noexcept;
    static u64 free_frames() noexcept;
};

} // namespace notyvos::mm
