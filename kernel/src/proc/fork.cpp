#include <kernel/libk/mem.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>

namespace notyvos::proc
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;
constexpr u64 kPageSize = 0x1000;
constexpr u64 kPhysMask = 0x000FFFFFFFFFF000ULL;

inline u64 align_down(u64 x)
{
    return x & ~(kPageSize - 1);
}

// Walk `pml4` and return a pointer to the leaf PTE for `va`, or nullptr if
// any level is absent. All reads go through the HHDM.
u64* leaf_pte(u64* pml4, uptr va) noexcept
{
    const u32 i4 = static_cast<u32>((va >> 39) & 0x1FF);
    const u32 i3 = static_cast<u32>((va >> 30) & 0x1FF);
    const u32 i2 = static_cast<u32>((va >> 21) & 0x1FF);
    const u32 i1 = static_cast<u32>((va >> 12) & 0x1FF);

    if (!(pml4[i4] & 1))
        return nullptr;
    auto* pdpt = reinterpret_cast<u64*>((pml4[i4] & kPhysMask) + kHhdm);
    if (!(pdpt[i3] & 1))
        return nullptr;
    auto* pd = reinterpret_cast<u64*>((pdpt[i3] & kPhysMask) + kHhdm);
    if (!(pd[i2] & 1))
        return nullptr;
    if (pd[i2] & (1ULL << 7))
        return nullptr; // 2MB huge page, skip
    auto* pt = reinterpret_cast<u64*>((pd[i2] & kPhysMask) + kHhdm);
    if (!(pt[i1] & 1))
        return nullptr;
    return &pt[i1];
}

// Walk or create intermediate tables in `dst` and return the leaf PTE slot.
u64* ensure_leaf(u64* dst, uptr va, u64 mid_flags) noexcept
{
    const u32 i4 = static_cast<u32>((va >> 39) & 0x1FF);
    const u32 i3 = static_cast<u32>((va >> 30) & 0x1FF);
    const u32 i2 = static_cast<u32>((va >> 21) & 0x1FF);
    const u32 i1 = static_cast<u32>((va >> 12) & 0x1FF);

    auto descend = [&](u64* parent, u32 idx) -> u64*
    {
        u64& e = parent[idx];
        if (e & 1)
            return reinterpret_cast<u64*>((e & kPhysMask) + kHhdm);
        const uptr p = mm::PhysicalMemory::allocate_frame();
        if (!p)
            return nullptr;
        auto* t = reinterpret_cast<u64*>(p + kHhdm);
        libk::memset(t, 0, kPageSize);
        e = p | mid_flags;
        return t;
    };

    u64* pdpt = descend(dst, i4);
    if (!pdpt)
        return nullptr;
    u64* pd = descend(pdpt, i3);
    if (!pd)
        return nullptr;
    u64* pt = descend(pd, i2);
    if (!pt)
        return nullptr;
    return &pt[i1];
}

} // namespace

// Copy every user-visible mapping in [lo, hi) from src_cr3 to dst_cr3.
// Intermediate tables in dst_cr3 are created as needed.
bool clone_user_range(uptr src_cr3, uptr dst_cr3, uptr lo, uptr hi) noexcept
{
    auto* src = reinterpret_cast<u64*>(src_cr3 + kHhdm);
    auto* dst = reinterpret_cast<u64*>(dst_cr3 + kHhdm);

    // Kernel half (256..511) must be cloned (shared) by the caller before
    // this function runs. We only touch the user half here.

    constexpr u64 kMidFlags = 1ULL | 2ULL | 4ULL; // P | W | U

    const uptr start = align_down(lo);
    const uptr end = (hi + kPageSize - 1) & ~(kPageSize - 1);

    for (uptr va = start; va < end; va += kPageSize)
    {
        u64* src_pte = leaf_pte(src, va);
        if (!src_pte)
            continue;
        const u64 src_entry = *src_pte;
        if (!(src_entry & 1))
            continue;

        const uptr new_phys = mm::PhysicalMemory::allocate_frame();
        if (!new_phys)
            return false;

        const uptr old_phys = src_entry & kPhysMask;
        libk::memcpy(reinterpret_cast<void*>(new_phys + kHhdm),
                     reinterpret_cast<const void*>(old_phys + kHhdm), kPageSize);

        const u64 leaf_flags = src_entry & 0xFFFULL; // P W U PWT PCD A D ...
        u64* dst_pte = ensure_leaf(dst, va, kMidFlags);
        if (!dst_pte)
            return false;
        *dst_pte = new_phys | leaf_flags;
    }
    return true;
}

} // namespace notyvos::proc
