#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/net/e1000.hpp>

namespace notyvos::net
{

namespace
{

constexpr u32 kREG_CTRL = 0x0000;
constexpr u32 kREG_ICR = 0x00C0;
constexpr u32 kREG_IMC = 0x00D8;
constexpr u32 kREG_RAL = 0x5400;
constexpr u32 kREG_RAH = 0x5404;

u8* g_mmio = nullptr;
MacAddress g_mac = {};
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

inline u32 reg_read(u32 off)
{
    return *reinterpret_cast<volatile u32*>(g_mmio + off);
}
inline void reg_write(u32 off, u32 v)
{
    *reinterpret_cast<volatile u32*>(g_mmio + off) = v;
}

bool find_e1000(u8* bus_out, u8* slot_out, u32* bar0_out)
{
    const u16 ids[] = {0x100E, 0x10D3, 0x1004, 0x100F, 0x1015};
    for (u32 bus = 0; bus < 8; ++bus)
    {
        for (u32 slot = 0; slot < 32; ++slot)
        {
            const u32 id = pci_read32(static_cast<u8>(bus), static_cast<u8>(slot), 0, 0);
            if ((id & 0xFFFF) != 0x8086)
                continue;
            const u16 dev = static_cast<u16>((id >> 16) & 0xFFFF);
            for (u16 want : ids)
            {
                if (dev == want)
                {
                    *bus_out = static_cast<u8>(bus);
                    *slot_out = static_cast<u8>(slot);
                    *bar0_out = pci_read32(*bus_out, *slot_out, 0, 0x10) & 0xFFFFFFF0u;
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

bool e1000_present() noexcept
{
    return g_present;
}
MacAddress e1000_mac() noexcept
{
    return g_mac;
}

bool e1000_init() noexcept
{
    u8 bus = 0, slot = 0;
    u32 bar0 = 0;
    if (!find_e1000(&bus, &slot, &bar0))
    {
        log::write(log::Level::Info, "e1000", "no Intel NIC found");
        return false;
    }
    log::write(log::Level::Info, "e1000", "found NIC at PCI %u:%u, BAR0=0x%llx",
               static_cast<unsigned long long>(bus), static_cast<unsigned long long>(slot),
               static_cast<unsigned long long>(bar0));

    u32 cmd = pci_read32(bus, slot, 0, 0x04);
    cmd |= 0x06;
    pci_write32(bus, slot, 0, 0x04, cmd);

    constexpr u64 kBarVa = 0xffffffffB0000000ULL;
    constexpr u64 kBarSize = 0x20000;
    for (u64 off = 0; off < kBarSize; off += 0x1000)
    {
        mm::VirtualMemory::map_page(static_cast<uptr>(kBarVa + off), static_cast<uptr>(bar0 + off),
                                    mm::page_flags::Present | mm::page_flags::Writable |
                                        mm::page_flags::PCD);
    }
    g_mmio = reinterpret_cast<u8*>(kBarVa);

    const u32 ral = reg_read(kREG_RAL);
    const u32 rah = reg_read(kREG_RAH);
    g_mac.b[0] = static_cast<u8>(ral & 0xFF);
    g_mac.b[1] = static_cast<u8>((ral >> 8) & 0xFF);
    g_mac.b[2] = static_cast<u8>((ral >> 16) & 0xFF);
    g_mac.b[3] = static_cast<u8>((ral >> 24) & 0xFF);
    g_mac.b[4] = static_cast<u8>(rah & 0xFF);
    g_mac.b[5] = static_cast<u8>((rah >> 8) & 0xFF);

    log::write(
        log::Level::Info, "e1000", "mac %02x:%02x:%02x:%02x:%02x:%02x",
        static_cast<unsigned long long>(g_mac.b[0]), static_cast<unsigned long long>(g_mac.b[1]),
        static_cast<unsigned long long>(g_mac.b[2]), static_cast<unsigned long long>(g_mac.b[3]),
        static_cast<unsigned long long>(g_mac.b[4]), static_cast<unsigned long long>(g_mac.b[5]));

    // Disable interrupts, clear pending, reset.
    reg_write(kREG_IMC, 0xFFFFFFFFu);
    (void)reg_read(kREG_ICR);
    reg_write(kREG_CTRL, reg_read(kREG_CTRL) | (1u << 26));
    for (u32 i = 0; i < 1000; ++i)
    {
        if ((reg_read(kREG_CTRL) & (1u << 26)) == 0)
            break;
    }
    (void)reg_read(kREG_ICR);

    // Re-enable bus master (reset clears it).
    cmd = pci_read32(bus, slot, 0, 0x04);
    cmd |= 0x06;
    pci_write32(bus, slot, 0, 0x04, cmd);

    g_present = true;
    log::write(log::Level::Info, "e1000", "controller ready (no link yet)");
    return true;
}

} // namespace notyvos::net
