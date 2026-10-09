#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/e1000.hpp>

namespace notyvos::net
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;

constexpr u32 kREG_CTRL  = 0x0000;
constexpr u32 kREG_ICR   = 0x00C0;
constexpr u32 kREG_IMC   = 0x00D8;
constexpr u32 kREG_RCTL  = 0x0100;
constexpr u32 kREG_TCTL  = 0x0400;
constexpr u32 kREG_RDBAL = 0x2800;
constexpr u32 kREG_RDBAH = 0x2804;
constexpr u32 kREG_RDLEN = 0x2808;
constexpr u32 kREG_RDH   = 0x2810;
constexpr u32 kREG_RDT   = 0x2818;
constexpr u32 kREG_TDBAL = 0x3800;
constexpr u32 kREG_TDBAH = 0x3804;
constexpr u32 kREG_TDLEN = 0x3808;
constexpr u32 kREG_TDH   = 0x3810;
constexpr u32 kREG_TDT   = 0x3818;
constexpr u32 kREG_RAL   = 0x5400;
constexpr u32 kREG_RAH   = 0x5404;

constexpr u32 kRCTL_EN     = 1u << 1;
constexpr u32 kRCTL_BAM    = 1u << 15;
constexpr u32 kRCTL_SECRC  = 1u << 26;
constexpr u32 kRCTL_BSIZE_2048 = 0u << 16;
constexpr u32 kTCTL_EN     = 1u << 1;
constexpr u32 kTCTL_PSP    = 1u << 3;
constexpr u32 kTCTL_CT_SHIFT = 4;
constexpr u32 kTCTL_COLD_SHIFT = 12;

constexpr u32 kRxCount = 32;
constexpr u32 kTxCount = 32;

struct RxDesc
{
    u64 buffer;
    u16 length;
    u16 checksum;
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed));

struct TxDesc
{
    u64 buffer;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
} __attribute__((packed));

static_assert(sizeof(RxDesc) == 16, "rx desc");
static_assert(sizeof(TxDesc) == 16, "tx desc");

u8*  g_mmio = nullptr;
u64  g_mmio_va = 0;
u32  g_mmio_size = 0;
Interface g_if;
bool g_ready = false;

RxDesc* g_rx_ring = nullptr;
u64     g_rx_ring_phys = 0;
u8*     g_rx_bufs[kRxCount] = {};
u32     g_rx_cur = 0;

TxDesc* g_tx_ring = nullptr;
u64     g_tx_ring_phys = 0;
u8*     g_tx_bufs[kTxCount] = {};
u32     g_tx_cur = 0;

inline void outl_(u16 p, u32 v) noexcept
{
    asm volatile("outl %0, %1" ::"a"(v), "Nd"(p));
}
inline u32 inl_(u16 p) noexcept
{
    u32 v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 off) noexcept
{
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) |
                     (static_cast<u32>(slot) << 11) | (static_cast<u32>(func) << 8) |
                     (off & 0xFC);
    outl_(0xCF8, addr);
    return inl_(0xCFC);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 value) noexcept
{
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) |
                     (static_cast<u32>(slot) << 11) | (static_cast<u32>(func) << 8) |
                     (off & 0xFC);
    outl_(0xCF8, addr);
    outl_(0xCFC, value);
}

inline u32 reg_read(u32 off) noexcept
{
    return *reinterpret_cast<volatile u32*>(g_mmio + off);
}
inline void reg_write(u32 off, u32 v) noexcept
{
    *reinterpret_cast<volatile u32*>(g_mmio + off) = v;
}

bool find_e1000(u8* bus_out, u8* slot_out, u32* bar0_out) noexcept
{
    const u16 ids[] = {0x100E, 0x10D3, 0x1004, 0x100F, 0x1015, 0x153A};
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

void clear_stats() noexcept
{
    g_if.rx_packets = 0;
    g_if.tx_packets = 0;
    g_if.rx_bytes = 0;
    g_if.tx_bytes = 0;
    g_if.rx_errors = 0;
    g_if.tx_errors = 0;
}

// Non-blocking RX drain. Called from the network poll loop or by the
// scheduler tick.
void rx_drain() noexcept
{
    for (u32 guard = 0; guard < kRxCount; ++guard)
    {
        RxDesc& d = g_rx_ring[g_rx_cur];
        if ((d.status & 0x01) == 0)
            break;

        const u16 len = d.length;
        if (len > 0 && len <= 2048)
        {
            receive_frame(&g_if, g_rx_bufs[g_rx_cur], len);
        }
        d.status = 0;
        d.length = 0;
        // Return this descriptor to hardware.
        reg_write(kREG_RDT, g_rx_cur);
        g_rx_cur = (g_rx_cur + 1) % kRxCount;
    }
}

bool tx_send(void* /*user*/, const u8* frame, usize len) noexcept
{
    if (len > 2048)
        return false;
    TxDesc& d = g_tx_ring[g_tx_cur];
    libk::memcpy(g_tx_bufs[g_tx_cur], frame, len);
    d.length = static_cast<u16>(len);
    d.cmd = 0x0B;   // EOP | IFCS | RS
    d.status = 0;
    d.cso = 0;
    d.css = 0;
    d.special = 0;
    reg_write(kREG_TDT, (g_tx_cur + 1) % kTxCount);
    g_tx_cur = (g_tx_cur + 1) % kTxCount;
    return true;
}

} // namespace

bool e1000_present() noexcept
{
    return g_ready;
}

Interface* e1000_interface() noexcept
{
    return g_ready ? &g_if : nullptr;
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
               static_cast<unsigned long long>(bus),
               static_cast<unsigned long long>(slot),
               static_cast<unsigned long long>(bar0));

    u32 cmd = pci_read32(bus, slot, 0, 0x04);
    cmd |= 0x06;   // memory + bus master
    pci_write32(bus, slot, 0, 0x04, cmd);

    g_mmio_size = 0x20000;
    g_mmio_va = static_cast<u64>(bar0) + kHhdm;
    for (u64 off = 0; off < g_mmio_size; off += 0x1000)
    {
        mm::VirtualMemory::map_page(static_cast<uptr>(g_mmio_va + off),
                                    static_cast<uptr>(bar0 + off),
                                    mm::page_flags::Present | mm::page_flags::Writable |
                                        mm::page_flags::PCD);
    }
    g_mmio = reinterpret_cast<u8*>(g_mmio_va);

    // MAC from RAL/RAH.
    const u32 ral = reg_read(kREG_RAL);
    const u32 rah = reg_read(kREG_RAH);
    g_if.mac[0] = static_cast<u8>(ral & 0xFF);
    g_if.mac[1] = static_cast<u8>((ral >> 8) & 0xFF);
    g_if.mac[2] = static_cast<u8>((ral >> 16) & 0xFF);
    g_if.mac[3] = static_cast<u8>((ral >> 24) & 0xFF);
    g_if.mac[4] = static_cast<u8>(rah & 0xFF);
    g_if.mac[5] = static_cast<u8>((rah >> 8) & 0xFF);

    char macstr[24];
    format_mac(g_if.mac, macstr);
    log::write(log::Level::Info, "e1000", "mac %s", macstr);

    // Reset.
    reg_write(kREG_IMC, 0xFFFFFFFFu);
    (void)reg_read(kREG_ICR);
    reg_write(kREG_CTRL, reg_read(kREG_CTRL) | (1u << 26));
    for (u32 i = 0; i < 1000; ++i)
        if ((reg_read(kREG_CTRL) & (1u << 26)) == 0)
            break;
    (void)reg_read(kREG_ICR);

    // Re-enable bus master after reset.
    cmd = pci_read32(bus, slot, 0, 0x04);
    cmd |= 0x06;
    pci_write32(bus, slot, 0, 0x04, cmd);

    // RX ring: allocate physically-contiguous descriptor array from PMM.
    g_rx_ring_phys = static_cast<u64>(mm::PhysicalMemory::allocate_contiguous(kRxCount / 4));
    if (g_rx_ring_phys == 0)
        return false;
    g_rx_ring = reinterpret_cast<RxDesc*>(g_rx_ring_phys + kHhdm);
    libk::memset(g_rx_ring, 0, sizeof(RxDesc) * kRxCount);

    for (u32 i = 0; i < kRxCount; ++i)
    {
        const u64 buf_phys = static_cast<u64>(mm::PhysicalMemory::allocate_frame());
        g_rx_bufs[i] = reinterpret_cast<u8*>(buf_phys + kHhdm);
        libk::memset(g_rx_bufs[i], 0, 2048);
        g_rx_ring[i].buffer = buf_phys;
    }

    reg_write(kREG_RDBAL, static_cast<u32>(g_rx_ring_phys & 0xFFFFFFFFu));
    reg_write(kREG_RDBAH, static_cast<u32>(g_rx_ring_phys >> 32));
    reg_write(kREG_RDLEN, sizeof(RxDesc) * kRxCount);
    reg_write(kREG_RDH, 0);
    reg_write(kREG_RDT, kRxCount - 1);

    reg_write(kREG_RCTL, kRCTL_EN | kRCTL_BAM | kRCTL_SECRC | kRCTL_BSIZE_2048);

    // TX ring.
    g_tx_ring_phys = static_cast<u64>(mm::PhysicalMemory::allocate_contiguous(kTxCount / 4));
    if (g_tx_ring_phys == 0)
        return false;
    g_tx_ring = reinterpret_cast<TxDesc*>(g_tx_ring_phys + kHhdm);
    libk::memset(g_tx_ring, 0, sizeof(TxDesc) * kTxCount);

    for (u32 i = 0; i < kTxCount; ++i)
    {
        const u64 buf_phys = static_cast<u64>(mm::PhysicalMemory::allocate_frame());
        g_tx_bufs[i] = reinterpret_cast<u8*>(buf_phys + kHhdm);
        libk::memset(g_tx_bufs[i], 0, 2048);
        g_tx_ring[i].buffer = buf_phys;
    }

    reg_write(kREG_TDBAL, static_cast<u32>(g_tx_ring_phys & 0xFFFFFFFFu));
    reg_write(kREG_TDBAH, static_cast<u32>(g_tx_ring_phys >> 32));
    reg_write(kREG_TDLEN, sizeof(TxDesc) * kTxCount);
    reg_write(kREG_TDH, 0);
    reg_write(kREG_TDT, 0);

    reg_write(kREG_TCTL,
              kTCTL_EN | kTCTL_PSP | (15u << kTCTL_CT_SHIFT) | (64u << kTCTL_COLD_SHIFT));

    clear_stats();

    g_if.name[0] = 'e'; g_if.name[1] = 't'; g_if.name[2] = 'h'; g_if.name[3] = '0';
    g_if.name[4] = 0;
    g_if.type = IfType::Ethernet;
    g_if.state = IfState::Up;
    g_if.mtu = 1500;
    g_if.ip = 0;
    g_if.netmask = 0;
    g_if.gateway = 0;
    g_if.dns = 0;
    g_if.tx = &tx_send;
    g_if.user = nullptr;

    (void)register_interface(&g_if);

    g_ready = true;
    log::write(log::Level::Info, "e1000", "controller ready (eth0 up)");
    return true;
}

// Public RX pump. Call from scheduler tick or idle loop.
extern "C" void notyvos_e1000_poll() noexcept
{
    if (!g_ready)
        return;
    rx_drain();
}

} // namespace notyvos::net
