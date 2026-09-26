#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/proc/elf.hpp>

namespace notyvos::proc
{

namespace
{

struct Elf64_Ehdr
{
    u8 e_ident[16];
    u16 e_type, e_machine;
    u32 e_version;
    u64 e_entry, e_phoff, e_shoff;
    u32 e_flags;
    u16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed));

struct Elf64_Phdr
{
    u32 p_type, p_flags;
    u64 p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} __attribute__((packed));

constexpr u32 kPtLoad = 1;
constexpr u32 kPfW = 2;
constexpr u64 kHhdm = 0xffff800000000000ULL;
constexpr u64 kPhysMask = 0x000FFFFFFFFFF000ULL;
constexpr u64 kPageSize = 0x1000ULL;

inline u64 align_down(u64 x) noexcept
{
    return x & ~(kPageSize - 1);
}
inline u64 align_up(u64 x) noexcept
{
    return (x + kPageSize - 1) & ~(kPageSize - 1);
}

// Walk the given PML4 and return the physical frame at the leaf, or 0.
// All intermediate allocations happen through HHDM. `mid_flags` is applied
// to newly-created intermediate tables.
u64* walk_or_create(u64* pml4, uptr va, bool create, u64 mid_flags) noexcept
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
        if (!create)
            return nullptr;
        const uptr p = mm::PhysicalMemory::allocate_frame();
        if (!p)
            return nullptr;
        auto* t = reinterpret_cast<u64*>(p + kHhdm);
        libk::memset(t, 0, kPageSize);
        e = p | mid_flags;
        return t;
    };

    u64* pdpt = descend(pml4, i4);
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

bool install_pte(u64* pml4, uptr va, uptr phys, u64 leaf_flags, u64 mid_flags) noexcept
{
    u64* slot = walk_or_create(pml4, va, true, mid_flags);
    if (!slot)
        return false;
    *slot = (phys & kPhysMask) | leaf_flags;
    return true;
}

} // namespace

ElfLoadResult load_elf(const void* image, usize size) noexcept
{
    ElfLoadResult res{};

    if (size < sizeof(Elf64_Ehdr))
    {
        log::write(log::Level::Error, "elf", "image too small");
        return res;
    }

    const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(image);
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F')
    {
        log::write(log::Level::Error, "elf", "bad magic");
        return res;
    }
    if (eh->e_ident[4] != 2 || eh->e_machine != 0x3E)
    {
        log::write(log::Level::Error, "elf", "unsupported class/machine");
        return res;
    }

    // Allocate new PML4.
    const uptr new_pml4_phys = mm::PhysicalMemory::allocate_frame();
    if (!new_pml4_phys)
    {
        log::write(log::Level::Error, "elf", "pml4 alloc failed");
        return res;
    }
    auto* new_pml4 = reinterpret_cast<u64*>(new_pml4_phys + kHhdm);
    libk::memset(new_pml4, 0, kPageSize);

    // Clone kernel higher-half entries (indices 256..511) from the current
    // CR3. We read CR3 directly rather than using VirtualMemory's cached
    // kernel PML4 so we always see the active one.
    uptr active_pml4_phys = 0;
    asm volatile("mov %%cr3, %0" : "=r"(active_pml4_phys));
    const auto* active_pml4 = reinterpret_cast<const u64*>(active_pml4_phys + kHhdm);
    for (u32 i = 256; i < 512; ++i)
        new_pml4[i] = active_pml4[i];

    // Intermediate tables must be kernel-accessible, writable, and user
    // so that ring 3 can reach the leaves we install.
    constexpr u64 kMidFlags =
        mm::page_flags::Present | mm::page_flags::Writable | mm::page_flags::User;

    u64 lo = ~0ULL, hi = 0;

    for (u32 i = 0; i < eh->e_phnum; ++i)
    {
        const u64 ph_off = eh->e_phoff + static_cast<u64>(i) * eh->e_phentsize;
        if (ph_off + sizeof(Elf64_Phdr) > size)
            break;

        const auto* ph =
            reinterpret_cast<const Elf64_Phdr*>(reinterpret_cast<const u8*>(image) + ph_off);
        if (ph->p_type != kPtLoad)
            continue;
        if (ph->p_memsz == 0)
            continue;

        const u64 seg_lo = align_down(ph->p_vaddr);
        const u64 seg_hi = align_up(ph->p_vaddr + ph->p_memsz);
        const u64 file_lo = ph->p_vaddr;
        const u64 file_hi = ph->p_vaddr + ph->p_filesz;
        const u8* file_base = reinterpret_cast<const u8*>(image) + ph->p_offset;

        if (seg_lo < lo)
            lo = seg_lo;
        if (seg_hi > hi)
            hi = seg_hi;

        u64 leaf_flags = mm::page_flags::Present | mm::page_flags::User;
        if (ph->p_flags & kPfW)
            leaf_flags |= mm::page_flags::Writable;

        for (u64 va = seg_lo; va < seg_hi; va += kPageSize)
        {
            const uptr phys = mm::PhysicalMemory::allocate_frame();
            if (!phys)
            {
                log::write(log::Level::Error, "elf", "frame alloc failed");
                return res;
            }
            auto* page = reinterpret_cast<u8*>(phys + kHhdm);
            libk::memset(page, 0, kPageSize);

            // Copy file bytes that land on this page.
            const u64 copy_start = (va > file_lo) ? va : file_lo;
            const u64 copy_end = ((va + kPageSize) < file_hi) ? (va + kPageSize) : file_hi;
            if (copy_end > copy_start)
            {
                const u64 src_off = copy_start - file_lo;
                const u64 dst_off = copy_start - va;
                libk::memcpy(page + dst_off, file_base + src_off,
                             static_cast<usize>(copy_end - copy_start));
            }

            if (!install_pte(new_pml4, va, phys, leaf_flags, kMidFlags))
            {
                log::write(log::Level::Error, "elf", "pte install failed va=0x%llx",
                           static_cast<unsigned long long>(va));
                return res;
            }
        }
    }

    // Allocate a user stack page above the highest loaded VA.
    uptr stack_top_va = 0;
    if (hi != 0)
    {
        const u64 stack_base = align_up(hi);
        const uptr stack_phys = mm::PhysicalMemory::allocate_frame();
        if (!stack_phys)
        {
            log::write(log::Level::Error, "elf", "stack alloc failed");
            return res;
        }
        libk::memset(reinterpret_cast<u8*>(stack_phys + kHhdm), 0, kPageSize);

        constexpr u64 kStackFlags =
            mm::page_flags::Present | mm::page_flags::Writable | mm::page_flags::User;

        if (!install_pte(new_pml4, stack_base, stack_phys, kStackFlags, kMidFlags))
        {
            log::write(log::Level::Error, "elf", "stack pte install failed");
            return res;
        }
        stack_top_va = stack_base + kPageSize - 16;
        if (hi < stack_base + kPageSize)
            hi = stack_base + kPageSize;
    }

    res.entry = eh->e_entry;
    res.user_lo = lo;
    res.user_hi = hi;
    res.cr3 = new_pml4_phys;
    res.stack_top = stack_top_va;

    log::write(
        log::Level::Info, "elf", "entry=0x%llx user=[0x%llx,0x%llx) cr3=0x%llx",
        static_cast<unsigned long long>(res.entry), static_cast<unsigned long long>(res.user_lo),
        static_cast<unsigned long long>(res.user_hi), static_cast<unsigned long long>(res.cr3));

    return res;
}

} // namespace notyvos::proc
