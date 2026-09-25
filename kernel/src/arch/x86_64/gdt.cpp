#include <kernel/arch/x86_64/gdt.hpp>
#include <kernel/arch/x86_64/tss.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

// Referenced from gdt_flush.S
extern "C" void notyvos_gdt_flush(const GdtPointer* ptr) noexcept;

namespace
{

// GDT: null + kcode + kdata + udata + ucode + tss_lo + tss_hi
constexpr u32 kGdtEntries = 7;
alignas(16) GdtEntry g_gdt[kGdtEntries] = {};
alignas(16) GdtPointer g_gdt_ptr = {};

constexpr u8 kPresent = 0x80;
constexpr u8 kRing0 = 0x00;
constexpr u8 kRing3 = 0x60;
constexpr u8 kSegment = 0x10; // code/data (not system)
constexpr u8 kExecutable = 0x08;
constexpr u8 kReadWrite = 0x02;

constexpr u8 kGranularity4K = 0x80;
constexpr u8 kLongMode = 0x20;
constexpr u8 kDefaultBig = 0x40; // D/B for data segments

void set_entry(u32 idx, u8 access, u8 flags, u32 limit, u64 base) noexcept
{
    g_gdt[idx].limit_low = static_cast<u16>(limit & 0xFFFF);
    g_gdt[idx].base_low = static_cast<u16>(base & 0xFFFF);
    g_gdt[idx].base_mid = static_cast<u8>((base >> 16) & 0xFF);
    g_gdt[idx].access = access;
    g_gdt[idx].flags_limit_high = static_cast<u8>(((limit >> 16) & 0x0F) | flags);
    g_gdt[idx].base_high = static_cast<u8>((base >> 24) & 0xFF);
}

void set_tss_descriptor(u32 idx, u64 base, u32 limit) noexcept
{
    const u8 access = 0x89; // present, DPL=0, type=1001 (64-bit TSS available)
    const u8 flags = 0x00;  // granularity byte, no 4K, no L bit

    g_gdt[idx].limit_low = static_cast<u16>(limit & 0xFFFF);
    g_gdt[idx].base_low = static_cast<u16>(base & 0xFFFF);
    g_gdt[idx].base_mid = static_cast<u8>((base >> 16) & 0xFF);
    g_gdt[idx].access = access;
    g_gdt[idx].flags_limit_high = static_cast<u8>(((limit >> 16) & 0x0F) | flags);
    g_gdt[idx].base_high = static_cast<u8>((base >> 24) & 0xFF);

    // High 8 bytes: base bits 32..63, then 32 reserved bits.
    // We treat them as the following two entries in the GDT array.
    auto* high = reinterpret_cast<u32*>(&g_gdt[idx + 1]);
    high[0] = static_cast<u32>((base >> 32) & 0xFFFFFFFFu);
    high[1] = 0;
}

} // namespace

void gdt_init() noexcept
{
    // 0: null descriptor
    set_entry(0, 0, 0, 0, 0);

    // 1: kernel code (ring 0, 64-bit, executable, readable)
    set_entry(1,
              kPresent | kRing0 | kSegment | kExecutable | kReadWrite, // 0x9A
              kGranularity4K | kLongMode,                              // 0xA0
              0xFFFFF, 0);

    // 2: kernel data (ring 0, writable)
    set_entry(2,
              kPresent | kRing0 | kSegment | kReadWrite, // 0x92
              kGranularity4K | kDefaultBig,              // 0xC0
              0xFFFFF, 0);

    // 3: user data (ring 3, writable). Must be immediately before user code
    //    for SYSRET semantics later.
    set_entry(3,
              kPresent | kRing3 | kSegment | kReadWrite, // 0xF2
              kGranularity4K | kDefaultBig,              // 0xC0
              0xFFFFF, 0);

    // 4: user code (ring 3, 64-bit, executable, readable)
    set_entry(4,
              kPresent | kRing3 | kSegment | kExecutable | kReadWrite, // 0xFA
              kGranularity4K | kLongMode,                              // 0xA0
              0xFFFFF, 0);

    // 5 + 6: TSS (16 bytes total)
    const auto& tss = tss_descriptor();
    set_tss_descriptor(5, tss.base, tss.limit);

    g_gdt_ptr.limit = sizeof(GdtEntry) * kGdtEntries - 1;
    g_gdt_ptr.base = reinterpret_cast<u64>(&g_gdt[0]);

    notyvos_gdt_flush(&g_gdt_ptr);

    log::write(log::Level::Info, "gdt", "installed: %u entries, base=0x%x",
               static_cast<unsigned>(kGdtEntries), static_cast<unsigned long long>(g_gdt_ptr.base));
}

} // namespace notyvos::arch::x86_64
