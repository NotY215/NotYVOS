#include <kernel/log.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::mm
{

namespace
{

u64 g_hhdm_offset = 0;
uptr g_kernel_pml4 = 0;

inline u64* phys_to_virt(uptr phys) noexcept
{
    return reinterpret_cast<u64*>(phys + g_hhdm_offset);
}

// Walk one level. `table_phys` is the physical address of the current table.
// Returns pointer to the next-level table's physical address slot, or nullptr
// if a mapping was absent and `create` was false.
u64* ensure_next(u64* table, u32 index, bool create, u64 child_flags) noexcept
{
    u64& entry = table[index];
    if (entry & page_flags::Present)
    {
        return phys_to_virt(entry & page_flags::kPhysicalMask);
    }
    if (!create)
        return nullptr;

    const uptr new_phys = PhysicalMemory::allocate_frame();
    if (!new_phys)
        return nullptr;

    u64* new_table = phys_to_virt(new_phys);
    for (usize i = 0; i < kPageTableEntries; ++i)
        new_table[i] = 0;

    entry = static_cast<u64>(new_phys) | child_flags;
    return new_table;
}

} // namespace

void VirtualMemory::init(u64 hhdm_offset) noexcept
{
    g_hhdm_offset = hhdm_offset;
    asm volatile("mov %%cr3, %0" : "=r"(g_kernel_pml4));
    log::write(log::Level::Info, "vmm", "kernel pml4 phys=0x%x hhdm=0x%x",
               static_cast<u64>(g_kernel_pml4), static_cast<u64>(g_hhdm_offset));
}

uptr VirtualMemory::kernel_pml4_physical() noexcept
{
    return g_kernel_pml4;
}

bool VirtualMemory::map_page(uptr virt, uptr phys, u64 flags) noexcept
{
    const u32 i4 = pml4_index(virt);
    const u32 i3 = pdpt_index(virt);
    const u32 i2 = pd_index(virt);
    const u32 i1 = pt_index(virt);

    u64* pml4 = phys_to_virt(g_kernel_pml4);
    u64* pdpt = ensure_next(pml4, i4, true, page_flags::Present | page_flags::Writable);
    if (!pdpt)
        return false;

    u64* pd = ensure_next(pdpt, i3, true, page_flags::Present | page_flags::Writable);
    if (!pd)
        return false;

    u64* pt = ensure_next(pd, i2, true, page_flags::Present | page_flags::Writable);
    if (!pt)
        return false;

    pt[i1] =
        (static_cast<u64>(phys) & page_flags::kPhysicalMask) | (flags & page_flags::kFlagsMask);

    flush_tlb_page(virt);
    return true;
}

bool VirtualMemory::unmap_page(uptr virt) noexcept
{
    const u32 i4 = pml4_index(virt);
    const u32 i3 = pdpt_index(virt);
    const u32 i2 = pd_index(virt);
    const u32 i1 = pt_index(virt);

    u64* pml4 = phys_to_virt(g_kernel_pml4);
    if (!(pml4[i4] & page_flags::Present))
        return false;

    u64* pdpt = phys_to_virt(pml4[i4] & page_flags::kPhysicalMask);
    if (!(pdpt[i3] & page_flags::Present))
        return false;

    u64* pd = phys_to_virt(pdpt[i3] & page_flags::kPhysicalMask);
    if (!(pd[i2] & page_flags::Present))
        return false;
    if (pd[i2] & page_flags::Huge)
        return false; // 2 MB huge page: skip

    u64* pt = phys_to_virt(pd[i2] & page_flags::kPhysicalMask);
    if (!(pt[i1] & page_flags::Present))
        return false;

    pt[i1] = 0;
    flush_tlb_page(virt);
    return true;
}

uptr VirtualMemory::get_physical(uptr virt) noexcept
{
    const u32 i4 = pml4_index(virt);
    const u32 i3 = pdpt_index(virt);
    const u32 i2 = pd_index(virt);
    const u32 i1 = pt_index(virt);

    u64* pml4 = phys_to_virt(g_kernel_pml4);
    if (!(pml4[i4] & page_flags::Present))
        return 0;

    u64* pdpt = phys_to_virt(pml4[i4] & page_flags::kPhysicalMask);
    if (!(pdpt[i3] & page_flags::Present))
        return 0;

    u64* pd = phys_to_virt(pdpt[i3] & page_flags::kPhysicalMask);
    if (!(pd[i2] & page_flags::Present))
        return 0;
    if (pd[i2] & page_flags::Huge)
    {
        return (pd[i2] & page_flags::kPhysicalMask) | (virt & 0x1FFFFFULL);
    }

    u64* pt = phys_to_virt(pd[i2] & page_flags::kPhysicalMask);
    if (!(pt[i1] & page_flags::Present))
        return 0;

    return (pt[i1] & page_flags::kPhysicalMask) | (virt & kPageOffset);
}

void VirtualMemory::flush_tlb_page(uptr virt) noexcept
{
    asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
}

void VirtualMemory::flush_tlb_all() noexcept
{
    asm volatile("mov %0, %%cr3" ::"r"(g_kernel_pml4) : "memory");
}

} // namespace notyvos::mm
