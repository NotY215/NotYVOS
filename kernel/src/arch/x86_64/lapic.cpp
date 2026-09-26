#include <kernel/arch/x86_64/lapic.hpp>
#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
constexpr u32 kMsrApicBase = 0x1B;
constexpr u64 kApicBaseMask = 0x000FFFFFFFFF000ULL;
constexpr u64 kApicBaseEnable = 1ULL << 11;
constexpr u64 kApicBaseBsp = 1ULL << 8;

constexpr u32 kApicRegId = 0x20;
constexpr u32 kApicRegEoi = 0xB0;

volatile u32* g_lapic_mmio = nullptr;
u64 g_hhdm_offset = 0;
u64 g_lapic_phys = 0;

inline u32 lapic_read(u32 reg) noexcept
{
    return g_lapic_mmio[reg / 4];
}
inline void lapic_write(u32 reg, u32 value) noexcept
{
    g_lapic_mmio[reg / 4] = value;
}
} // namespace

void Lapic::map_mmio(u64 phys) noexcept
{
    const uptr virt = static_cast<uptr>(phys + g_hhdm_offset);
    if (!mm::VirtualMemory::map_page(virt, static_cast<uptr>(phys),
                                     mm::page_flags::Present | mm::page_flags::Writable |
                                         mm::page_flags::PCD))
    {
        log::write(log::Level::Error, "lapic", "map MMIO 0x%llx failed",
                   static_cast<unsigned long long>(phys));
        return;
    }
    g_lapic_mmio = reinterpret_cast<volatile u32*>(virt);
}

void Lapic::init_bsp(u64 hhdm_offset) noexcept
{
    g_hhdm_offset = hhdm_offset;

    const u64 base = rdmsr(kMsrApicBase);
    const u64 phys = base & kApicBaseMask;
    g_lapic_phys = phys;

    map_mmio(phys);
    if (!g_lapic_mmio)
        return;

    wrmsr(kMsrApicBase, base | kApicBaseEnable | kApicBaseBsp);

    log::write(log::Level::Info, "lapic", "bsp id=%llu base=0x%llx",
               static_cast<unsigned long long>(lapic_read(kApicRegId) >> 24),
               static_cast<unsigned long long>(phys));
}

void Lapic::enable_for_ap() noexcept
{
    const u64 base = rdmsr(kMsrApicBase);
    const u64 phys = base & kApicBaseMask;
    g_lapic_mmio = reinterpret_cast<volatile u32*>(phys + g_hhdm_offset);
    wrmsr(kMsrApicBase, (base & ~kApicBaseBsp) | kApicBaseEnable);
}

u32 Lapic::id() noexcept
{
    return g_lapic_mmio ? (lapic_read(kApicRegId) >> 24) : 0;
}
void Lapic::send_eoi() noexcept
{
    if (g_lapic_mmio)
        lapic_write(kApicRegEoi, 0);
}
bool Lapic::is_present() noexcept
{
    return g_lapic_mmio != nullptr;
}

} // namespace notyvos::arch::x86_64
