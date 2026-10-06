#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/usb/xhci.hpp>

namespace notyvos::usb::xhci
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;

// ---------------------------------------------------------------------------
// PCI I/O
// ---------------------------------------------------------------------------
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
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) | (static_cast<u32>(slot) << 11) |
                     (static_cast<u32>(func) << 8) | (off & 0xFCu);
    outl_(0xCF8, addr);
    return inl_(0xCFC);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 v) noexcept
{
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) | (static_cast<u32>(slot) << 11) |
                     (static_cast<u32>(func) << 8) | (off & 0xFCu);
    outl_(0xCF8, addr);
    outl_(0xCFC, v);
}

// ---------------------------------------------------------------------------
// MMIO helpers
// ---------------------------------------------------------------------------
inline u32 mmio_read32(const Controller* c, u32 off) noexcept
{
    return *reinterpret_cast<volatile u32*>(c->mmio + off);
}
inline void mmio_write32(const Controller* c, u32 off, u32 v) noexcept
{
    *reinterpret_cast<volatile u32*>(c->mmio + off) = v;
}
inline void mmio_write64(const Controller* c, u32 off, u64 v) noexcept
{
    *reinterpret_cast<volatile u32*>(c->mmio + off) = static_cast<u32>(v & 0xFFFFFFFFu);
    *reinterpret_cast<volatile u32*>(c->mmio + off + 4) = static_cast<u32>(v >> 32);
}

// ---------------------------------------------------------------------------
// Register offsets
// ---------------------------------------------------------------------------
constexpr u32 kCapCaplen = 0x00;
constexpr u32 kCapHciversion = 0x02;
constexpr u32 kCapHcsparams1 = 0x04;
constexpr u32 kCapDbOff = 0x14;
constexpr u32 kCapRtsoff = 0x18;

constexpr u32 kOpUsbcmd = 0x00;
constexpr u32 kOpUsbsts = 0x04;
constexpr u32 kOpCrCr = 0x18;
constexpr u32 kOpDcbaap = 0x30;
constexpr u32 kOpConfig = 0x38;

constexpr u32 kUsbcmdRs = 1u << 0;
constexpr u32 kUsbcmdHcrst = 1u << 1;
constexpr u32 kUsbcmdInte = 1u << 2;
constexpr u32 kUsbstsHch = 1u << 0;
constexpr u32 kUsbstsCnr = 1u << 11;

constexpr u32 kRtIr0 = 0x00;
constexpr u32 kIrImAn = 0x00;
constexpr u32 kIrImErstsz = 0x08;
constexpr u32 kIrErstba = 0x10;
constexpr u32 kIrErdp = 0x18;

constexpr u32 kPortScBase = 0x400;
constexpr u32 kPortStep = 0x10;

constexpr u32 kPortCcs = 1u << 0;
constexpr u32 kPortPed = 1u << 1;
constexpr u32 kPortOca = 1u << 3;
constexpr u32 kPortPr = 1u << 4;
constexpr u32 kPortPp = 1u << 9;
constexpr u32 kPortCsc = 1u << 17;
constexpr u32 kPortWrc = 1u << 19;
constexpr u32 kPortPrc = 1u << 21;
constexpr u32 kPortCec = 1u << 23;

// Rate-limit for over-current warnings. Some chipsets (including
// VirtualBox's emulated xHCI) report spurious OCA on unconnected ports.
// Log the first few, then stay silent.
constexpr u32 kMaxOcaLogs = 3;
u32 g_oca_logs = 0;

// ---------------------------------------------------------------------------
// TRB
// ---------------------------------------------------------------------------
struct Trb
{
    u32 dw0, dw1, dw2, dw3;
};

constexpr u32 kTrbNormal = 1;
constexpr u32 kTrbSetupStage = 2;
constexpr u32 kTrbDataStage = 3;
constexpr u32 kTrbStatusStage = 4;
constexpr u32 kTrbLink = 6;
constexpr u32 kTrbEnableSlot = 9;
constexpr u32 kTrbAddressDevice = 11;
constexpr u32 kTrbConfigureEp = 12;
constexpr u32 kTrbTransferEvent = 32;
constexpr u32 kTrbCmdComplete = 33;

constexpr u8 kCcSuccess = 1;
constexpr u8 kCcShortPkt = 13;

constexpr u32 kEpStateRunning = 1;

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------
u64 alloc_dma_frame(u8** virt_out) noexcept
{
    const u64 phys = static_cast<u64>(mm::PhysicalMemory::allocate_frame());
    if (phys == 0)
    {
        *virt_out = nullptr;
        return 0;
    }
    u8* v = reinterpret_cast<u8*>(phys + kHhdm);
    libk::memset(v, 0, 4096);
    *virt_out = v;
    return phys;
}

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
Controller g_controllers[kMaxControllers];
u32 g_count = 0;
Device g_devices[kMaxDevices];
u32 g_device_count = 0;

struct RingState
{
    u32 cmd_pos;
    u32 cmd_cycle;
    u32 evt_pos;
    u32 evt_cycle;
    u8* cmd_virt;
    u8* evt_virt;
};
RingState g_rings[kMaxControllers];

struct SlotState
{
    bool used;
    u8 port;
    u8 speed;
    u16 max_packet;
    u64 input_ctx_phys;
    u8* input_ctx_virt;
    u64 device_ctx_phys;
    u64 ep0_ring_phys;
    u8* ep0_ring_virt;
    u32 ep0_pos;
    u32 ep0_cycle;
};
SlotState g_slots[kMaxControllers][64];

// Per-endpoint transfer rings for bulk / interrupt endpoints.
struct EpRing
{
    bool used;
    u64 ring_phys;
    u8* ring_virt;
    u32 pos;
    u32 cycle;
};

constexpr u32 kEpsPerSlot = 32;
EpRing g_ep_rings[kMaxControllers][64][kEpsPerSlot];

SlotState* slot_of(u32 ci, u8 slot_id) noexcept
{
    if (ci >= kMaxControllers || slot_id >= 64)
        return nullptr;
    SlotState& s = g_slots[ci][slot_id];
    return s.used ? &s : nullptr;
}

// ---------------------------------------------------------------------------
// Command ring
// ---------------------------------------------------------------------------
bool cmd_enqueue(u32 ci, const Trb& t) noexcept
{
    if (ci >= kMaxControllers)
        return false;
    RingState& r = g_rings[ci];
    if (!r.cmd_virt)
        return false;

    Controller& c = g_controllers[ci];
    Trb* ring = reinterpret_cast<Trb*>(r.cmd_virt);

    if (r.cmd_pos == 255)
    {
        Trb link{};
        link.dw0 = static_cast<u32>(c.cmd_ring_phys & 0xFFFFFFFFu);
        link.dw1 = static_cast<u32>(c.cmd_ring_phys >> 32);
        link.dw2 = 0;
        link.dw3 = (kTrbLink << 10) | (1u << 1) | r.cmd_cycle;
        ring[255] = link;
        r.cmd_cycle ^= 1;
        r.cmd_pos = 0;
    }

    Trb copy = t;
    copy.dw3 = (copy.dw3 & ~1u) | r.cmd_cycle;
    ring[r.cmd_pos] = copy;
    ++r.cmd_pos;

    const u32 dboff = mmio_read32(&c, kCapDbOff) & 0xFFFFFFFCu;
    *reinterpret_cast<volatile u32*>(c.mmio + dboff) = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Event ring
// ---------------------------------------------------------------------------
bool event_poll(u32 ci, Trb& out) noexcept
{
    if (ci >= kMaxControllers)
        return false;
    RingState& r = g_rings[ci];
    if (!r.evt_virt)
        return false;

    Trb* ring = reinterpret_cast<Trb*>(r.evt_virt);
    const Trb& t = ring[r.evt_pos];
    if ((t.dw3 & 1u) != r.evt_cycle)
        return false;

    out = t;
    ++r.evt_pos;
    if (r.evt_pos == 256)
    {
        r.evt_pos = 0;
        r.evt_cycle ^= 1;
    }

    Controller& c = g_controllers[ci];
    const u32 rtsoff = mmio_read32(&c, kCapRtsoff) & 0xFFFFFFE0u;
    const u64 erdp = c.event_ring_phys + static_cast<u64>(r.evt_pos) * sizeof(Trb);
    mmio_write64(&c, rtsoff + kRtIr0 + kIrErdp, erdp | (1u << 3));
    return true;
}

bool wait_cmd_complete(u32 ci, u8& out_completion, u8& out_slot) noexcept
{
    for (u32 spin = 0; spin < 2000000u; ++spin)
    {
        Trb evt{};
        if (event_poll(ci, evt))
        {
            const u32 type = (evt.dw3 >> 10) & 0x3Fu;
            if (type == kTrbCmdComplete)
            {
                out_completion = static_cast<u8>(evt.dw2 & 0xFFu);
                out_slot = static_cast<u8>((evt.dw2 >> 16) & 0xFFu);
                return true;
            }
        }
        asm volatile("pause");
    }
    return false;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
bool cmd_enable_slot(u32 ci, u8& slot_id) noexcept
{
    Trb t{};
    t.dw3 = (kTrbEnableSlot << 10);
    if (!cmd_enqueue(ci, t))
        return false;

    u8 completion = 0;
    u8 ret_slot = 0;
    if (!wait_cmd_complete(ci, completion, ret_slot))
        return false;
    if (completion != kCcSuccess)
        return false;
    slot_id = ret_slot;
    return true;
}

bool cmd_address_device(u32 ci, u8 slot_id, u64 input_ctx_phys, bool bsr) noexcept
{
    Trb t{};
    t.dw0 = static_cast<u32>(input_ctx_phys & 0xFFFFFFFFu);
    t.dw1 = static_cast<u32>(input_ctx_phys >> 32);
    t.dw2 = static_cast<u32>(slot_id) << 24;
    t.dw3 = (kTrbAddressDevice << 10) | (bsr ? (1u << 9) : 0u);
    if (!cmd_enqueue(ci, t))
        return false;

    u8 completion = 0;
    u8 ret_slot = 0;
    if (!wait_cmd_complete(ci, completion, ret_slot))
        return false;
    return completion == kCcSuccess;
}

bool cmd_configure_endpoint(u32 ci, u8 slot_id, u64 input_ctx_phys) noexcept
{
    Trb t{};
    t.dw0 = static_cast<u32>(input_ctx_phys & 0xFFFFFFFFu);
    t.dw1 = static_cast<u32>(input_ctx_phys >> 32);
    t.dw2 = static_cast<u32>(slot_id) << 24;
    t.dw3 = (kTrbConfigureEp << 10);
    if (!cmd_enqueue(ci, t))
        return false;

    u8 completion = 0;
    u8 ret_slot = 0;
    if (!wait_cmd_complete(ci, completion, ret_slot))
        return false;
    return completion == kCcSuccess;
}

// ---------------------------------------------------------------------------
// EP0 control transfers
// ---------------------------------------------------------------------------
bool ep0_enqueue_trb(u32 ci, u8 slot_id, const Trb& t) noexcept
{
    if (slot_id >= 64)
        return false;
    SlotState& s = g_slots[ci][slot_id];
    if (!s.used || !s.ep0_ring_virt)
        return false;

    Trb* ring = reinterpret_cast<Trb*>(s.ep0_ring_virt);

    if (s.ep0_pos == 255)
    {
        Trb link{};
        link.dw0 = static_cast<u32>(s.ep0_ring_phys & 0xFFFFFFFFu);
        link.dw1 = static_cast<u32>(s.ep0_ring_phys >> 32);
        link.dw2 = 0;
        link.dw3 = (kTrbLink << 10) | (1u << 1) | s.ep0_cycle;
        ring[255] = link;
        s.ep0_cycle ^= 1;
        s.ep0_pos = 0;
    }

    Trb copy = t;
    copy.dw3 = (copy.dw3 & ~1u) | s.ep0_cycle;
    ring[s.ep0_pos] = copy;
    ++s.ep0_pos;

    const Controller& dc = g_controllers[ci];
    const u32 dboff = mmio_read32(&dc, kCapDbOff) & 0xFFFFFFFCu;
    *reinterpret_cast<volatile u32*>(dc.mmio + dboff + static_cast<u32>(slot_id) * 4u) = 1;
    return true;
}

bool wait_transfer_event(u32 ci, u8& out_cc, u32& out_remaining) noexcept
{
    for (u32 spin = 0; spin < 2000000u; ++spin)
    {
        Trb evt{};
        if (event_poll(ci, evt))
        {
            const u32 type = (evt.dw3 >> 10) & 0x3Fu;
            if (type == kTrbTransferEvent)
            {
                out_cc = static_cast<u8>(evt.dw2 & 0xFFu);
                out_remaining = (evt.dw2 >> 24) & 0xFFFFFFu;
                return true;
            }
        }
        asm volatile("pause");
    }
    return false;
}

// Generic control transfer with configurable direction.
bool control_transfer(u32 ci, u8 slot_id, u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex,
                      void* data, u16 wLength, bool data_in) noexcept
{
    if (slot_id >= 64)
        return false;
    SlotState& s = g_slots[ci][slot_id];
    if (!s.used)
        return false;

    u8* data_virt = reinterpret_cast<u8*>(data);
    u64 data_phys = 0;
    if (wLength > 0)
    {
        data_phys =
            static_cast<u64>(mm::VirtualMemory::get_physical(reinterpret_cast<uptr>(data_virt)));
        if (data_phys == 0)
            return false;
    }

    Trb setup{};
    setup.dw0 = static_cast<u32>(bmRequestType) | (static_cast<u32>(bRequest) << 8) |
                (static_cast<u32>(wValue) << 16);
    setup.dw1 = static_cast<u32>(wIndex) | (static_cast<u32>(wLength) << 16);
    setup.dw2 = 8;
    setup.dw3 = (kTrbSetupStage << 10) | (1u << 6);
    if (!ep0_enqueue_trb(ci, slot_id, setup))
        return false;

    if (wLength > 0)
    {
        Trb dt{};
        dt.dw0 = static_cast<u32>(data_phys & 0xFFFFFFFFu);
        dt.dw1 = static_cast<u32>(data_phys >> 32);
        dt.dw2 = wLength;
        dt.dw3 = (kTrbDataStage << 10) | (data_in ? (1u << 16) : 0u);
        if (!ep0_enqueue_trb(ci, slot_id, dt))
            return false;
    }

    Trb status{};
    status.dw3 = (kTrbStatusStage << 10) | (1u << 5) | (data_in ? 0u : (1u << 16));
    if (!ep0_enqueue_trb(ci, slot_id, status))
        return false;

    u8 cc = 0;
    u32 rem = 0;
    if (!wait_transfer_event(ci, cc, rem))
        return false;
    return (cc == kCcSuccess) || (cc == kCcShortPkt);
}

bool control_transfer_in(u32 ci, u8 slot_id, u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex,
                         void* data, u16 wLength) noexcept
{
    return control_transfer(ci, slot_id, bmRequestType, bRequest, wValue, wIndex, data, wLength,
                            true);
}

// ---------------------------------------------------------------------------
// Bulk / interrupt endpoint ring
// ---------------------------------------------------------------------------
bool ep_enqueue(u32 ci, u8 slot_id, u32 ep_index, const Trb& t) noexcept
{
    if (ci >= kMaxControllers || slot_id >= 64 || ep_index >= kEpsPerSlot)
        return false;
    EpRing& r = g_ep_rings[ci][slot_id][ep_index];
    if (!r.used || !r.ring_virt)
        return false;

    Trb* ring = reinterpret_cast<Trb*>(r.ring_virt);

    if (r.pos == 255)
    {
        Trb link{};
        link.dw0 = static_cast<u32>(r.ring_phys & 0xFFFFFFFFu);
        link.dw1 = static_cast<u32>(r.ring_phys >> 32);
        link.dw2 = 0;
        link.dw3 = (kTrbLink << 10) | (1u << 1) | r.cycle;
        ring[255] = link;
        r.cycle ^= 1;
        r.pos = 0;
    }

    Trb copy = t;
    copy.dw3 = (copy.dw3 & ~1u) | r.cycle;
    ring[r.pos] = copy;
    ++r.pos;

    const Controller& dc = g_controllers[ci];
    const u32 dboff = mmio_read32(&dc, kCapDbOff) & 0xFFFFFFFCu;
    *reinterpret_cast<volatile u32*>(dc.mmio + dboff + static_cast<u32>(slot_id) * 4u) = ep_index;
    return true;
}

bool wait_ep_event(u32 ci, u32 ep_index, u8& out_cc, u32& out_residual) noexcept
{
    const u32 want_ep_num = ep_index / 2;
    for (u32 spin = 0; spin < 4000000u; ++spin)
    {
        Trb evt{};
        if (event_poll(ci, evt))
        {
            const u32 type = (evt.dw3 >> 10) & 0x3Fu;
            if (type == kTrbTransferEvent)
            {
                const u32 ep = (evt.dw2 >> 16) & 0x1Fu;
                if (ep == want_ep_num)
                {
                    out_cc = static_cast<u8>(evt.dw2 & 0xFFu);
                    out_residual = (evt.dw2 >> 24) & 0xFFFFFFu;
                    return true;
                }
            }
        }
        asm volatile("pause");
    }
    return false;
}

// ---------------------------------------------------------------------------
// Slot bring-up
// ---------------------------------------------------------------------------
bool setup_slot(u32 ci, u8 slot_id, u8 port, u8 speed) noexcept
{
    if (slot_id == 0 || slot_id >= 64)
        return false;
    Controller& c = g_controllers[ci];
    SlotState& s = g_slots[ci][slot_id];

    u8* ictx_virt = nullptr;
    const u64 ictx_phys = alloc_dma_frame(&ictx_virt);
    if (ictx_phys == 0)
        return false;

    u8* dctx_virt = nullptr;
    const u64 dctx_phys = alloc_dma_frame(&dctx_virt);
    if (dctx_phys == 0)
        return false;

    u8* ep0_virt = nullptr;
    const u64 ep0_phys = alloc_dma_frame(&ep0_virt);
    if (ep0_phys == 0)
        return false;

    u64* dcbaa = reinterpret_cast<u64*>(c.dcbaa_phys + kHhdm);
    dcbaa[slot_id] = dctx_phys;

    u32* ic = reinterpret_cast<u32*>(ictx_virt);
    ic[0] = 0;
    ic[1] = (1u << 0) | (1u << 1);
    for (u32 i = 2; i < 8; ++i)
        ic[i] = 0;

    u32* sc = ic + 8;
    sc[0] = (static_cast<u32>(speed) << 20) | (1u << 27);
    sc[1] = static_cast<u32>(port) << 8;
    for (u32 i = 2; i < 8; ++i)
        sc[i] = 0;

    u32* ep0 = ic + 16;
    ep0[0] = kEpStateRunning | (3u << 30);
    ep0[1] = s.max_packet;
    ep0[2] = (8u << 24);
    ep0[3] = static_cast<u32>((ep0_phys & ~0xFu) | 1u);
    ep0[4] = static_cast<u32>(ep0_phys >> 32);
    for (u32 i = 5; i < 8; ++i)
        ep0[i] = 0;

    if (!cmd_address_device(ci, slot_id, ictx_phys, false))
        return false;

    s.used = true;
    s.port = port;
    s.speed = speed;
    s.input_ctx_phys = ictx_phys;
    s.input_ctx_virt = ictx_virt;
    s.device_ctx_phys = dctx_phys;
    s.ep0_ring_phys = ep0_phys;
    s.ep0_ring_virt = ep0_virt;
    s.ep0_pos = 0;
    s.ep0_cycle = 1;
    return true;
}

// ---------------------------------------------------------------------------
// Enumerate one port
// ---------------------------------------------------------------------------
struct DeviceDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u16 bcdUSB;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8 iManufacturer;
    u8 iProduct;
    u8 iSerialNumber;
    u8 bNumConfigurations;
} __attribute__((packed));

bool enumerate_port(u32 ci, u8 port, u8 speed) noexcept
{
    u8 slot_id = 0;
    if (!cmd_enable_slot(ci, slot_id))
        return false;
    if (slot_id == 0 || slot_id >= 64)
        return false;

    u16 max_packet = 64;
    if (speed == 2)
        max_packet = 8;
    else if (speed == 1)
        max_packet = 64;
    else if (speed == 3)
        max_packet = 64;
    else if (speed == 4)
        max_packet = 64;

    g_slots[ci][slot_id].used = false;
    g_slots[ci][slot_id].max_packet = max_packet;
    g_slots[ci][slot_id].port = port;
    g_slots[ci][slot_id].speed = speed;

    if (!setup_slot(ci, slot_id, port, speed))
        return false;

    alignas(64) u8 desc_buf[256];
    libk::memset(desc_buf, 0, sizeof(desc_buf));

    const bool dev_ok = control_transfer_in(ci, slot_id, 0x80, 6, (1u << 8), 0, desc_buf, 18);
    if (!dev_ok)
        return false;

    const auto* dd = reinterpret_cast<const DeviceDescriptor*>(desc_buf);
    if (dd->bLength < 18 || dd->bDescriptorType != 1)
        return false;

    if (g_device_count < kMaxDevices)
    {
        Device& dev = g_devices[g_device_count++];
        dev.present = true;
        dev.controller_index = ci;
        dev.slot_id = slot_id;
        dev.port = port;
        dev.speed = speed;
        dev.device_class = dd->bDeviceClass;
        dev.device_subclass = dd->bDeviceSubClass;
        dev.device_protocol = dd->bDeviceProtocol;
        dev.vendor_id = dd->idVendor;
        dev.product_id = dd->idProduct;
        dev.num_configurations = dd->bNumConfigurations;
    }

    log::write(log::Level::Info, "xhci",
               "device slot=%llu port=%llu speed=%llu vid=0x%llx pid=0x%llx class=0x%llx",
               static_cast<unsigned long long>(slot_id), static_cast<unsigned long long>(port),
               static_cast<unsigned long long>(speed),
               static_cast<unsigned long long>(dd->idVendor),
               static_cast<unsigned long long>(dd->idProduct),
               static_cast<unsigned long long>(dd->bDeviceClass));
    return true;
}

// ---------------------------------------------------------------------------
// PCI + MMIO bring-up
// ---------------------------------------------------------------------------
bool find_xhci(u8& bus, u8& slot, u8& func, u32& bar0) noexcept
{
    for (u32 b = 0; b < 8; ++b)
    {
        for (u32 s = 0; s < 32; ++s)
        {
            const u32 id = pci_read32(static_cast<u8>(b), static_cast<u8>(s), 0, 0);
            if ((id & 0xFFFFu) == 0xFFFFu)
                continue;
            const u32 classrev = pci_read32(static_cast<u8>(b), static_cast<u8>(s), 0, 8);
            const u8 cls = static_cast<u8>((classrev >> 24) & 0xFFu);
            const u8 sub = static_cast<u8>((classrev >> 16) & 0xFFu);
            const u8 pi = static_cast<u8>((classrev >> 8) & 0xFFu);
            if (cls == 0x0Cu && sub == 0x03u && pi == 0x30u)
            {
                bus = static_cast<u8>(b);
                slot = static_cast<u8>(s);
                func = 0;
                bar0 = pci_read32(bus, slot, func, 0x10) & 0xFFFFFFF0u;
                return true;
            }
        }
    }
    return false;
}

bool map_mmio(Controller& c) noexcept
{
    constexpr u64 kSize = 0x10000;
    for (u64 off = 0; off < kSize; off += 0x1000)
    {
        if (!mm::VirtualMemory::map_page(
                static_cast<uptr>(c.mmio_phys + off), static_cast<uptr>(c.mmio_phys + off),
                mm::page_flags::Present | mm::page_flags::Writable | mm::page_flags::PCD))
            return false;
    }
    c.mmio = reinterpret_cast<u8*>(c.mmio_phys);
    return true;
}

bool wait_stopped(Controller& c, u32 op) noexcept
{
    for (u32 i = 0; i < 1000000u; ++i)
    {
        if (mmio_read32(&c, op + kOpUsbsts) & kUsbstsHch)
            return true;
        asm volatile("pause");
    }
    return false;
}

bool wait_ready(Controller& c, u32 op) noexcept
{
    for (u32 i = 0; i < 1000000u; ++i)
    {
        if ((mmio_read32(&c, op + kOpUsbsts) & kUsbstsCnr) == 0)
            return true;
        asm volatile("pause");
    }
    return false;
}

bool bring_up(Controller& c, u32 ci) noexcept
{
    if (ci >= kMaxControllers)
        return false;

    const u32 caplen = mmio_read32(&c, kCapCaplen) & 0xFFu;
    if (caplen == 0)
        return false;

    const u32 hcs1 = mmio_read32(&c, kCapHcsparams1);
    c.max_ports = static_cast<u8>(hcs1 & 0xFFu);
    c.max_slots = static_cast<u8>((hcs1 >> 8) & 0xFFu);
    if (c.max_ports > kMaxPorts)
        c.max_ports = kMaxPorts;
    if (c.max_ports == 0)
        return false;

    c.hci_version = static_cast<u16>(mmio_read32(&c, kCapHciversion) & 0xFFFFu);

    const u32 rtsoff = mmio_read32(&c, kCapRtsoff) & 0xFFFFFFE0u;

    u32 cmd = mmio_read32(&c, caplen + kOpUsbcmd);
    cmd &= ~kUsbcmdRs;
    mmio_write32(&c, caplen + kOpUsbcmd, cmd);
    if (!wait_stopped(c, caplen))
        return false;

    mmio_write32(&c, caplen + kOpUsbcmd, cmd | kUsbcmdHcrst);
    for (u32 i = 0; i < 1000000u; ++i)
    {
        if ((mmio_read32(&c, caplen + kOpUsbcmd) & kUsbcmdHcrst) == 0)
            break;
        asm volatile("pause");
    }
    if (!wait_ready(c, caplen))
        return false;

    u8* dcbaa_virt = nullptr;
    c.dcbaa_phys = alloc_dma_frame(&dcbaa_virt);
    if (c.dcbaa_phys == 0)
        return false;
    mmio_write64(&c, caplen + kOpDcbaap, c.dcbaa_phys);
    mmio_write32(&c, caplen + kOpConfig, static_cast<u32>(c.max_slots) & 0xFFu);

    u8* cmd_virt = nullptr;
    c.cmd_ring_phys = alloc_dma_frame(&cmd_virt);
    if (c.cmd_ring_phys == 0)
        return false;
    mmio_write64(&c, caplen + kOpCrCr, c.cmd_ring_phys | 1u);

    u8* evt_virt = nullptr;
    c.event_ring_phys = alloc_dma_frame(&evt_virt);
    if (c.event_ring_phys == 0)
        return false;

    u8* erst_virt = nullptr;
    c.erst_phys = alloc_dma_frame(&erst_virt);
    if (c.erst_phys == 0)
        return false;
    *reinterpret_cast<volatile u64*>(erst_virt + 0) =
        (c.event_ring_phys & ~static_cast<u64>(0x3Fu)) | 256u;
    *reinterpret_cast<volatile u64*>(erst_virt + 8) = 0;

    const u32 ir0 = rtsoff + kRtIr0;
    mmio_write32(&c, ir0 + kIrImErstsz, 1u);
    mmio_write64(&c, ir0 + kIrErstba, c.erst_phys);
    mmio_write64(&c, ir0 + kIrErdp, c.event_ring_phys);
    mmio_write32(&c, ir0 + kIrImAn, 0u);

    cmd |= kUsbcmdRs;
    cmd &= ~kUsbcmdInte;
    mmio_write32(&c, caplen + kOpUsbcmd, cmd);
    c.started = true;

    g_rings[ci].cmd_virt = cmd_virt;
    g_rings[ci].evt_virt = evt_virt;
    g_rings[ci].cmd_pos = 0;
    g_rings[ci].cmd_cycle = 1;
    g_rings[ci].evt_pos = 0;
    g_rings[ci].evt_cycle = 1;
    return true;
}

void scan_ports(Controller& c, u32 ci) noexcept
{
    const u32 op = mmio_read32(&c, kCapCaplen) & 0xFFu;
    const u32 port_base = op + kPortScBase;

    for (u8 p = 1; p <= c.max_ports; ++p)
    {
        const u32 off = port_base + static_cast<u32>(p - 1) * kPortStep;
        u32 sc = mmio_read32(&c, off);

        if ((sc & kPortPp) == 0)
        {
            sc |= kPortPp;
            sc |= kPortCsc | kPortPrc | kPortWrc | kPortCec;
            mmio_write32(&c, off, sc);
            for (u32 i = 0; i < 100000u; ++i)
                asm volatile("pause");
            sc = mmio_read32(&c, off);
        }

        if ((sc & kPortCcs) == 0)
            continue;

        if (sc & kPortOca)
        {
            sc |= kPortCsc | kPortPrc | kPortWrc | kPortCec;
            mmio_write32(&c, off, sc);
            if (g_oca_logs < kMaxOcaLogs)
            {
                ++g_oca_logs;
                log::write(log::Level::Warn, "xhci", "port %llu: over-current; skipped",
                           static_cast<unsigned long long>(p));
            }
            continue;
        }

        const u8 speed = static_cast<u8>((sc >> 10) & 0xFu);
        if (speed < 1 || speed > 4)
        {
            log::write(log::Level::Warn, "xhci", "port %llu: invalid speed code %llu; skipped",
                       static_cast<unsigned long long>(p), static_cast<unsigned long long>(speed));
            continue;
        }

        log::write(log::Level::Info, "xhci", "port %llu: device attached (speed code %llu)",
                   static_cast<unsigned long long>(p), static_cast<unsigned long long>(speed));

        (void)enumerate_port(ci, p, speed);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void init() noexcept
{
    g_count = 0;
    g_device_count = 0;
    g_oca_logs = 0;
    libk::memset(g_controllers, 0, sizeof(g_controllers));
    libk::memset(g_devices, 0, sizeof(g_devices));
    libk::memset(g_rings, 0, sizeof(g_rings));
    libk::memset(g_slots, 0, sizeof(g_slots));
    libk::memset(g_ep_rings, 0, sizeof(g_ep_rings));

    u8 bus = 0, slot = 0, func = 0;
    u32 bar0 = 0;
    if (!find_xhci(bus, slot, func, bar0))
    {
        log::write(log::Level::Info, "xhci", "no xHCI controller found");
        return;
    }

    const u32 ci = 0;
    Controller& c = g_controllers[ci];
    c.present = true;
    c.bus = bus;
    c.slot = slot;
    c.func = func;
    c.mmio_phys = static_cast<u64>(bar0);
    c.mmio = nullptr;

    log::write(log::Level::Info, "xhci", "found controller at PCI %llu:%llu BAR0=0x%llx",
               static_cast<unsigned long long>(bus), static_cast<unsigned long long>(slot),
               static_cast<unsigned long long>(bar0));

    u32 cmdreg = pci_read32(bus, slot, func, 0x04);
    cmdreg |= 0x06;
    pci_write32(bus, slot, func, 0x04, cmdreg);

    if (!map_mmio(c))
    {
        log::write(log::Level::Warn, "xhci", "MMIO map failed");
        c.present = false;
        return;
    }

    if (!bring_up(c, ci))
    {
        log::write(log::Level::Warn, "xhci", "bring-up failed");
        c.present = false;
        return;
    }

    g_count = 1;

    log::write(log::Level::Info, "xhci", "controller ready: version=%llu ports=%llu slots=%llu",
               static_cast<unsigned long long>(c.hci_version),
               static_cast<unsigned long long>(c.max_ports),
               static_cast<unsigned long long>(c.max_slots));

    scan_ports(c, ci);

    log::write(log::Level::Info, "xhci", "USB: %llu controller(s), %llu device(s)",
               static_cast<unsigned long long>(g_count),
               static_cast<unsigned long long>(g_device_count));
}

u32 controller_count() noexcept
{
    return g_count;
}

const Controller* controller(u32 index) noexcept
{
    if (index >= g_count)
        return nullptr;
    return &g_controllers[index];
}

u32 device_count() noexcept
{
    return g_device_count;
}

const Device* device(u32 index) noexcept
{
    if (index >= g_device_count)
        return nullptr;
    return &g_devices[index];
}

PortState port_state(u32 ci, u8 port) noexcept
{
    if (ci >= g_count)
        return PortState::Error;
    const Controller& c = g_controllers[ci];
    if (port == 0 || port > c.max_ports)
        return PortState::Error;

    const u32 op = mmio_read32(&c, kCapCaplen) & 0xFFu;
    const u32 off = op + kPortScBase + static_cast<u32>(port - 1) * kPortStep;
    const u32 sc = mmio_read32(&c, off);

    if ((sc & kPortCcs) == 0)
        return PortState::Disconnected;
    if (sc & kPortOca)
        return PortState::Error;
    if (sc & kPortPed)
        return PortState::Enabled;
    if (sc & kPortPr)
        return PortState::Reset;
    return PortState::Powered;
}

bool set_configuration(u32 ci, u8 slot_id, u8 config_value) noexcept
{
    return control_transfer(ci, slot_id, 0x00, 9, static_cast<u16>(config_value), 0, nullptr, 0,
                            false);
}

bool configure_endpoint(u32 ci, u8 slot_id, u8 ep_num, EpDir dir, EpType type, u16 max_packet,
                        u8 interval) noexcept
{
    SlotState* s = slot_of(ci, slot_id);
    if (!s)
        return false;
    if (ep_num == 0 || ep_num > 15)
        return false;

    const u32 ep_index =
        static_cast<u32>(ep_num) * 2u + static_cast<u32>(dir == EpDir::In ? 1u : 0u);
    if (ep_index >= kEpsPerSlot)
        return false;

    u8* ring_virt = nullptr;
    const u64 ring_phys = alloc_dma_frame(&ring_virt);
    if (ring_phys == 0)
        return false;

    EpRing& r = g_ep_rings[ci][slot_id][ep_index];
    r.used = true;
    r.ring_phys = ring_phys;
    r.ring_virt = ring_virt;
    r.pos = 0;
    r.cycle = 1;

    u32* ic = reinterpret_cast<u32*>(s->input_ctx_virt);
    libk::memset(ic, 0, 33 * 32);

    ic[1] = (1u << 0) | (1u << ep_num);

    u32* sc = ic + 8;
    sc[0] = (static_cast<u32>(s->speed) << 20) | (static_cast<u32>(ep_num) << 27);
    sc[1] = static_cast<u32>(s->port) << 8;

    u32* ep = ic + 8 + static_cast<u32>(ep_num) * 8;
    ep[0] = kEpStateRunning | (3u << 30);
    ep[1] = static_cast<u32>(max_packet);
    const u32 avg_len = (type == EpType::Interrupt) ? 8u : 1024u;
    ep[2] = avg_len << 24;
    ep[3] = static_cast<u32>((ring_phys & ~0xFu) | 1u);
    ep[4] = static_cast<u32>(ring_phys >> 32);
    ep[5] = static_cast<u32>(interval);

    if (!cmd_configure_endpoint(ci, slot_id, s->input_ctx_phys))
    {
        log::write(log::Level::Warn, "xhci", "Configure Endpoint ep=%llu failed",
                   static_cast<unsigned long long>(ep_num));
        return false;
    }
    return true;
}

isize bulk_transfer(u32 ci, u8 slot_id, u8 ep_num, EpDir dir, void* data, u32 length) noexcept
{
    SlotState* s = slot_of(ci, slot_id);
    if (!s)
        return -1;
    if (length == 0 || length > 4096)
        return -1;

    const u32 ep_index =
        static_cast<u32>(ep_num) * 2u + static_cast<u32>(dir == EpDir::In ? 1u : 0u);

    const u64 data_phys =
        static_cast<u64>(mm::VirtualMemory::get_physical(reinterpret_cast<uptr>(data)));
    if (data_phys == 0)
        return -1;

    Trb t{};
    t.dw0 = static_cast<u32>(data_phys & 0xFFFFFFFFu);
    t.dw1 = static_cast<u32>(data_phys >> 32);
    t.dw2 = length;
    t.dw3 = (kTrbNormal << 10) | (1u << 5) | (dir == EpDir::In ? (1u << 16) : 0u);
    if (!ep_enqueue(ci, slot_id, ep_index, t))
        return -1;

    u8 cc = 0;
    u32 residual = 0;
    if (!wait_ep_event(ci, ep_index, cc, residual))
        return -1;
    if (cc != kCcSuccess && cc != kCcShortPkt)
        return -1;

    return static_cast<isize>(length - residual);
}

isize interrupt_poll(u32 ci, u8 slot_id, u8 ep_num, EpDir dir, void* data, u32 length) noexcept
{
    SlotState* s = slot_of(ci, slot_id);
    if (!s)
        return -1;
    if (length == 0 || length > 1024)
        return -1;

    const u32 ep_index =
        static_cast<u32>(ep_num) * 2u + static_cast<u32>(dir == EpDir::In ? 1u : 0u);
    if (ep_index >= kEpsPerSlot)
        return -1;

    EpRing& r = g_ep_rings[ci][slot_id][ep_index];
    if (!r.used)
        return -1;

    const u64 data_phys =
        static_cast<u64>(mm::VirtualMemory::get_physical(reinterpret_cast<uptr>(data)));
    if (data_phys == 0)
        return -1;

    Trb t{};
    t.dw0 = static_cast<u32>(data_phys & 0xFFFFFFFFu);
    t.dw1 = static_cast<u32>(data_phys >> 32);
    t.dw2 = length;
    t.dw3 = (kTrbNormal << 10) | (1u << 5) | (dir == EpDir::In ? (1u << 16) : 0u);
    if (!ep_enqueue(ci, slot_id, ep_index, t))
        return -1;

    for (u32 spin = 0; spin < 200000u; ++spin)
    {
        Trb evt{};
        if (event_poll(ci, evt))
        {
            const u32 type = (evt.dw3 >> 10) & 0x3Fu;
            if (type == kTrbTransferEvent)
            {
                const u32 ep = (evt.dw2 >> 16) & 0x1Fu;
                if (ep == ep_num)
                {
                    const u8 cc = static_cast<u8>(evt.dw2 & 0xFFu);
                    const u32 residual = (evt.dw2 >> 24) & 0xFFFFFFu;
                    if (cc == kCcSuccess || cc == kCcShortPkt)
                        return static_cast<isize>(length - residual);
                    return -1;
                }
            }
        }
        asm volatile("pause");
    }
    return 0;
}

isize get_descriptor(u32 ci, u8 slot_id, u8 desc_type, u8 desc_index, u16 lang_id, void* data,
                     u16 length) noexcept
{
    const u16 wValue = static_cast<u16>((static_cast<u16>(desc_type) << 8) | desc_index);
    const u16 wIndex = lang_id;
    return control_transfer(ci, slot_id, 0x80, 6, wValue, wIndex, data, length, true)
               ? static_cast<isize>(length)
               : -1;
}

} // namespace notyvos::usb::xhci
