#pragma once
#include <kernel/types.hpp>

namespace notyvos::proc
{

struct ElfLoadResult
{
    uptr entry;     // virtual entry point
    uptr user_lo;   // min user VA loaded (page-aligned)
    uptr user_hi;   // max user VA loaded (page-aligned, exclusive)
    uptr cr3;       // new PML4 physical
    uptr stack_top; // initial RSP (virtual)
};

// Load an ELF64 image at `image` of `size` bytes. Creates a new PML4 with
// the kernel's higher-half entries cloned. Populates PT_LOAD segments.
// Allocates one user stack page below the highest loaded VA (or at a fixed
// location if the image has none).
ElfLoadResult load_elf(const void* image, usize size) noexcept;

} // namespace notyvos::proc
