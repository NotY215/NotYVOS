#include <kernel/block/ahci.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::block
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;
constexpr u32 kMaxPrdt = 8;

inline void outl_(u16 port, u32 v)
{
    asm volatile("outl %0, %1" ::"a"(v), "Nd"(port));
}
inline u32 inl_(u16 port)
{
    u32 v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
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

struct HbaPort
{
    u32 clb, clbu, fb, fbu, is, ie, cmd, rsv0, tfd, sig, ssts, sctl, serr, sact, ci, sntf, fbs,
        rsv1[11], vendor[4];
} __attribute__((packed));

struct HbaMem
{
    u32 cap, ghc, is, pi, vs, ccc_ctl, ccc_pts, em_loc, em_ctl, cap2, bohc;
    u8 rsv[0xA0 - 0x2C];
    u8 vendor[0x100 - 0xA0];
    HbaPort ports[32];
} __attribute__((packed));

struct CmdHeader
{
    u16 opts;
    u16 prdtl;
    u32 prdbc;
    u32 ctba;
    u32 ctbau;
    u32 rsv[4];
} __attribute__((packed));

struct PrdtEntry
{
    u32 dba, dbau, rsv, dbc;
} __attribute__((packed));

struct CmdTable
{
    u8 cfis[64];
    u8 acmd[16];
    u8 rsv[48];
    PrdtEntry prdt[kMaxPrdt];
} __attribute__((packed));

struct AhciPort
{
    HbaPort* port;
    CmdHeader* cmd_list;
    CmdTable* ct;
    u64 cmd_list_phys;
    u64 ct_phys;
    u64 fis_phys;
    u64 sector_count;
};

AhciPort g_ports[32];
u32 g_port_count = 0;
HbaMem* g_hba = nullptr;

bool find_ahci(u8* bus_out, u8* slot_out, u32* bar5_out)
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
            const u8 pi = static_cast<u8>((classrev >> 8) & 0xFF);
            if (cls == 0x01 && sub == 0x06 && pi == 0x01)
            {
                *bus_out = static_cast<u8>(bus);
                *slot_out = static_cast<u8>(slot);
                *bar5_out = pci_read32(*bus_out, *slot_out, 0, 0x24) & 0xFFFFFFF0u;
                return true;
            }
        }
    }
    return false;
}

bool ahci_issue(AhciPort* p, u32 slot, u32 cmd, u64 lba, u32 count, uptr buf_phys, u32 bytes,
                bool write)
{
    for (u32 i = 0; i < 1000000; ++i)
    {
        if ((p->port->tfd & 0x88) == 0)
            break;
        asm volatile("pause");
    }

    CmdHeader* ch = &p->cmd_list[slot];
    ch->prdtl = 0;
    ch->prdbc = 0;
    ch->opts = static_cast<u16>(5 | (write ? 0x20 : 0));

    u8* fis = p->ct->cfis;
    libk::memset(fis, 0, 64);
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = static_cast<u8>(cmd);
    fis[4] = static_cast<u8>(lba & 0xFF);
    fis[5] = static_cast<u8>((lba >> 8) & 0xFF);
    fis[6] = static_cast<u8>((lba >> 16) & 0xFF);
    fis[7] = 0x40;
    fis[8] = static_cast<u8>((lba >> 24) & 0xFF);
    fis[9] = static_cast<u8>((lba >> 32) & 0xFF);
    fis[10] = static_cast<u8>((lba >> 40) & 0xFF);
    fis[12] = static_cast<u8>(count & 0xFF);
    fis[13] = static_cast<u8>((count >> 8) & 0xFF);

    p->ct->prdt[0].dba = static_cast<u32>(buf_phys & 0xFFFFFFFFULL);
    p->ct->prdt[0].dbau = static_cast<u32>(buf_phys >> 32);
    p->ct->prdt[0].rsv = 0;
    p->ct->prdt[0].dbc = (bytes - 1) | (1u << 31);
    ch->prdtl = 1;

    p->port->is = 0xFFFFFFFF;
    p->port->ci = (1u << slot);

    for (u32 i = 0; i < 5000000; ++i)
    {
        const u32 ci = p->port->ci;
        const u32 tfd = p->port->tfd;
        if ((ci & (1u << slot)) == 0)
        {
            if (tfd & 0x01)
                return false;
            return true;
        }
        asm volatile("pause");
    }
    return false;
}

uptr virt_to_phys(uptr virt)
{
    return mm::VirtualMemory::get_physical(virt);
}

// Bounce buffer to work around PRDT requiring a physical address.
// 4 KiB, page-aligned, kernel-owned.
alignas(4096) u8 g_bounce[4096];

isize ahci_read_impl(BlockDevice* dev, u64 lba, u32 count, void* buf)
{
    auto* p = static_cast<AhciPort*>(dev->driver_data);
    if (!p)
        return -1;
    const u32 bytes = count * 512;
    if (bytes == 0)
        return 0;
    if (bytes > sizeof(g_bounce))
        return -1;

    const uptr phys = virt_to_phys(reinterpret_cast<uptr>(g_bounce));
    if (!phys)
        return -1;

    const bool ok = ahci_issue(p, 0, 0x25, lba, count, phys, bytes, false);
    if (!ok)
        return -1;
    libk::memcpy(buf, g_bounce, bytes);
    return static_cast<isize>(bytes);
}

isize ahci_write_impl(BlockDevice* dev, u64 lba, u32 count, const void* buf)
{
    auto* p = static_cast<AhciPort*>(dev->driver_data);
    if (!p)
        return -1;
    const u32 bytes = count * 512;
    if (bytes == 0)
        return 0;
    if (bytes > sizeof(g_bounce))
        return -1;

    libk::memcpy(g_bounce, buf, bytes);
    const uptr phys = virt_to_phys(reinterpret_cast<uptr>(g_bounce));
    if (!phys)
        return -1;

    const bool ok = ahci_issue(p, 0, 0x35, lba, count, phys, bytes, true);
    return ok ? static_cast<isize>(bytes) : -1;
}

void ahci_identify(AhciPort* p)
{
    const uptr buf_phys = mm::PhysicalMemory::allocate_frame();
    if (!buf_phys)
        return;
    auto* buf = reinterpret_cast<u16*>(buf_phys + kHhdm);
    libk::memset(buf, 0, 512);

    const bool ok = ahci_issue(p, 1, 0xEC, 0, 0, buf_phys, 512, false);
    if (ok)
    {
        const u64 lo = static_cast<u64>(buf[100]) | (static_cast<u64>(buf[101]) << 16);
        const u64 hi = static_cast<u64>(buf[102]) | (static_cast<u64>(buf[103]) << 16);
        p->sector_count = (hi << 32) | lo;
    }
    mm::PhysicalMemory::free_frame(buf_phys);
}

} // namespace

void ahci_init() noexcept
{
    u8 bus = 0, slot = 0;
    u32 bar5 = 0;
    if (!find_ahci(&bus, &slot, &bar5))
    {
        log::write(log::Level::Warn, "ahci", "no AHCI controller found");
        return;
    }
    log::write(log::Level::Info, "ahci", "found controller at PCI %u:%u, ABAR=0x%llx",
               static_cast<unsigned long long>(bus), static_cast<unsigned long long>(slot),
               static_cast<unsigned long long>(bar5));

    u32 cmd = pci_read32(bus, slot, 0, 0x04);
    cmd |= 0x06;
    pci_write32(bus, slot, 0, 0x04, cmd);

    constexpr u64 kAbarVa = 0xffffffffA0000000ULL;
    constexpr u64 kAbarSize = 0x2000;
    for (u64 off = 0; off < kAbarSize; off += 0x1000)
    {
        mm::VirtualMemory::map_page(static_cast<uptr>(kAbarVa + off), static_cast<uptr>(bar5 + off),
                                    mm::page_flags::Present | mm::page_flags::Writable |
                                        mm::page_flags::PCD);
    }
    g_hba = reinterpret_cast<HbaMem*>(kAbarVa);

    g_hba->ghc |= (1u << 31);

    const u32 pi = g_hba->pi;
    log::write(log::Level::Info, "ahci", "ports implemented bitmap = 0x%llx",
               static_cast<unsigned long long>(pi));

    for (u32 i = 0; i < 32; ++i)
    {
        if ((pi & (1u << i)) == 0)
            continue;
        HbaPort* port = &g_hba->ports[i];
        if ((port->ssts & 0x0F) != 0x03)
            continue;
        if (port->sig != 0x00000101)
            continue;

        const uptr cl_phys = mm::PhysicalMemory::allocate_frame();
        const uptr fi_phys = mm::PhysicalMemory::allocate_frame();
        const uptr ct_phys = mm::PhysicalMemory::allocate_frame();
        if (!cl_phys || !fi_phys || !ct_phys)
            continue;

        libk::memset(reinterpret_cast<void*>(cl_phys + kHhdm), 0, 0x1000);
        libk::memset(reinterpret_cast<void*>(fi_phys + kHhdm), 0, 0x1000);
        libk::memset(reinterpret_cast<void*>(ct_phys + kHhdm), 0, 0x1000);

        port->cmd &= ~static_cast<u32>(0x0001);
        while (port->cmd & 0x8000)
            asm volatile("pause");

        port->clb = static_cast<u32>(cl_phys & 0xFFFFFFFFULL);
        port->clbu = static_cast<u32>(cl_phys >> 32);
        port->fb = static_cast<u32>(fi_phys & 0xFFFFFFFFULL);
        port->fbu = static_cast<u32>(fi_phys >> 32);

        auto* cl = reinterpret_cast<CmdHeader*>(cl_phys + kHhdm);
        cl[0].ctba = static_cast<u32>(ct_phys & 0xFFFFFFFFULL);
        cl[0].ctbau = static_cast<u32>(ct_phys >> 32);
        cl[1].ctba = static_cast<u32>(ct_phys & 0xFFFFFFFFULL);
        cl[1].ctbau = static_cast<u32>(ct_phys >> 32);

        port->cmd |= 0x0010;
        port->cmd |= 0x0001;
        port->ie = 0xFFFFFFFF;

        AhciPort& ap = g_ports[g_port_count];
        ap.port = port;
        ap.cmd_list = cl;
        ap.cmd_list_phys = cl_phys;
        ap.ct = reinterpret_cast<CmdTable*>(ct_phys + kHhdm);
        ap.ct_phys = ct_phys;
        ap.fis_phys = fi_phys;
        ap.sector_count = 0;

        ahci_identify(&ap);

        auto* bd = static_cast<BlockDevice*>(mm::Heap::allocate(sizeof(BlockDevice)));
        libk::memset(bd, 0, sizeof(BlockDevice));
        libk::strncpy(bd->name, "sda", sizeof(bd->name) - 1);
        bd->sector_count = ap.sector_count;
        bd->logical_sector_size = 512;
        bd->driver_data = &ap;
        bd->read_sectors = ahci_read_impl;
        bd->write_sectors = ahci_write_impl;
        bd->read_only = false;

        block_register(bd);
        ++g_port_count;
    }

    log::write(log::Level::Info, "ahci", "%llu SATA disk(s) registered",
               static_cast<unsigned long long>(g_port_count));
}

} // namespace notyvos::block
