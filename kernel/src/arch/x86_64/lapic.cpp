#include <kernel/arch/x86_64/cpuid.hpp>
#include <kernel/arch/x86_64/lapic.hpp>
#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
constexpr u32 kMsrApicBase = 0x1B;
constexpr u64 kApicBaseMask = 0x000FFFFFFFFF000ULL;
constexpr u64 kApicBaseEnable = 1ULL << 11;
constexpr u64 kApicBaseBsp = 1ULL << 8;

constexpr u32 kApicRegId = 0x20;  // LAPIC ID register
constexpr u32 kApicRegEoi = 0xB0; // End-of-interrupt

volatile u32* g_lapic_mmio = nullptr;
} // namespace

static inline u32 lapic_read(u32 reg) noexcept
{
    return g_lapic_mmio[reg / 4];
}
static inline void lapic_write(u32 reg, u32 value) noexcept
{
    g_lapic_mmio[reg / 4] = value;
}

void Lapic::init_bsp() noexcept
{
    const u64 base = rdmsr(kMsrApicBase);
    const u64 phys = base & kApicBaseMask;
    // LAPIC MMIO is below 4 GB on QEMU; the HHDM maps it for us. We need the
    // HHDM offset. For now assume physical 0xFEE00000 is available through
    // the HHDM (Limine guarantees this).
    const u64 hhdm = 0xffff800000000000ULL;
    g_lapic_mmio = reinterpret_cast<volatile u32*>(phys + hhdm);

    // Ensure LAPIC is enabled (bit 11) and remains BSP (bit 8).
    wrmsr(kMsrApicBase, base | kApicBaseEnable | kApicBaseBsp);

    log::write(log::Level::Info, "lapic", "bsp id=%u base=0x%llx",
               static_cast<u64>(lapic_read(kApicRegId) >> 24), phys);
}

void Lapic::enable_for_ap() noexcept
{
    const u64 base = rdmsr(kMsrApicBase);
    const u64 phys = base & kApicBaseMask;
    const u64 hhdm = 0xffff800000000000ULL;
    g_lapic_mmio = reinterpret_cast<volatile u32*>(phys + hhdm);
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
