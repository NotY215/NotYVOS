#include <kernel/audio/hda.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::audio
{

namespace
{

constexpr u32 kREG_GCAP = 0x00;
constexpr u32 kREG_VMIN = 0x02;
constexpr u32 kREG_VMAJ = 0x03;
constexpr u32 kREG_GCTL = 0x08;
constexpr u32 kREG_STATESTS = 0x0E;

constexpr u32 kGCTL_CRST = 1u << 0;
constexpr u16 kSTATESTS_MASK = 0x7FFF;

u8* g_mmio = nullptr;
u32 g_codec_mask = 0;
u32 g_codec_count = 0;
bool g_present = false;

inline void outl_(u16 p, u32 v)
{
    asm volatile("outl %0, %1" ::"a"(v), "Nd"(p));
}
inline u32 inl_(u16 p)
{
    u32 v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 off)
{
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) | (static_cast<u32>(slot) << 11) |
                     (static_cast<u32>(func) << 8) | (off & 0xFC);
    outl_(0xCF8, addr);
    return inl_(0xCFC);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 value)
{
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) | (static_cast<u32>(slot) << 11) |
                     (static_cast<u32>(func) << 8) | (off & 0xFC);
    outl_(0xCF8, addr);
    outl_(0xCFC, value);
}

inline u8 mmio_r8(u32 off)
{
    return *reinterpret_cast<volatile u8*>(g_mmio + off);
}
inline u16 mmio_r16(u32 off)
{
    return *reinterpret_cast<volatile u16*>(g_mmio + off);
}
inline u32 mmio_r32(u32 off)
{
    return *reinterpret_cast<volatile u32*>(g_mmio + off);
}
inline void mmio_w32(u32 off, u32 v)
{
    *reinterpret_cast<volatile u32*>(g_mmio + off) = v;
}
inline void mmio_w16(u32 off, u16 v)
{
    *reinterpret_cast<volatile u16*>(g_mmio + off) = v;
}

bool find_hda(u8* bus_out, u8* slot_out, u32* bar0_out)
{
    for (u32 bus = 0; bus < 8; ++bus)
    {
        for (u32 slot = 0; slot < 32; ++slot)
        {
            const u32 id = pci_read32(static_cast<u8>(bus), static_cast<u8>(slot), 0, 0);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            const u32 classrev = pci_read32(static_cast<u8>(bus), static_cast<u8>(slot), 0, 8);
            const u8 cls = static_cast<u8>((classrev >> 24) & 0xFF);
            const u8 sub = static_cast<u8>((classrev >> 16) & 0xFF);
            if (cls == 0x04 && sub == 0x03)
            {
                *bus_out = static_cast<u8>(bus);
                *slot_out = static_cast<u8>(slot);
                *bar0_out = pci_read32(*bus_out, *slot_out, 0, 0x10) & 0xFFFFFFF0u;
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool hda_present() noexcept
{
    return g_present;
}
u32 hda_codec_count() noexcept
{
    return g_codec_count;
}

bool hda_init() noexcept
{
    u8 bus = 0, slot = 0;
    u32 bar0 = 0;
    if (!find_hda(&bus, &slot, &bar0))
    {
        log::write(log::Level::Info, "hda", "no HDA controller found");
        return false;
    }
    log::write(log::Level::Info, "hda", "found controller at PCI %u:%u, BAR0=0x%llx",
               static_cast<unsigned long long>(bus), static_cast<unsigned long long>(slot),
               static_cast<unsigned long long>(bar0));

    u32 cmd = pci_read32(bus, slot, 0, 0x04);
    cmd |= 0x06;
    pci_write32(bus, slot, 0, 0x04, cmd);

    constexpr u64 kBarVa = 0xffffffffC0000000ULL;
    constexpr u64 kBarSize = 0x4000;
    for (u64 off = 0; off < kBarSize; off += 0x1000)
    {
        mm::VirtualMemory::map_page(static_cast<uptr>(kBarVa + off), static_cast<uptr>(bar0 + off),
                                    mm::page_flags::Present | mm::page_flags::Writable |
                                        mm::page_flags::PCD);
    }
    g_mmio = reinterpret_cast<u8*>(kBarVa);

    const u16 gcap = mmio_r16(kREG_GCAP);
    const u8 vmin = mmio_r8(kREG_VMIN);
    const u8 vmaj = mmio_r8(kREG_VMAJ);
    log::write(log::Level::Info, "hda", "gcap=0x%llx version %llu.%llu",
               static_cast<unsigned long long>(gcap), static_cast<unsigned long long>(vmaj),
               static_cast<unsigned long long>(vmin));

    mmio_w32(kREG_GCTL, mmio_r32(kREG_GCTL) & ~kGCTL_CRST);
    for (u32 i = 0; i < 10000; ++i)
    {
        if ((mmio_r32(kREG_GCTL) & kGCTL_CRST) == 0)
            break;
    }
    mmio_w32(kREG_GCTL, mmio_r32(kREG_GCTL) | kGCTL_CRST);
    for (u32 i = 0; i < 10000; ++i)
    {
        if ((mmio_r32(kREG_GCTL) & kGCTL_CRST) != 0)
            break;
    }
    for (u32 i = 0; i < 10000; ++i)
    {
        if (mmio_r16(kREG_STATESTS) != 0)
            break;
    }
    g_codec_mask = static_cast<u32>(mmio_r16(kREG_STATESTS)) & static_cast<u32>(kSTATESTS_MASK);
    // FIXED: explicit cast u32 -> u16.
    mmio_w16(kREG_STATESTS, static_cast<u16>(g_codec_mask));

    for (u32 i = 0; i < 15; ++i)
    {
        if (g_codec_mask & (1u << i))
            ++g_codec_count;
    }

    log::write(log::Level::Info, "hda", "codec mask 0x%llx, %llu codec(s)",
               static_cast<unsigned long long>(g_codec_mask),
               static_cast<unsigned long long>(g_codec_count));

    g_present = true;
    return true;
}

} // namespace notyvos::audio
