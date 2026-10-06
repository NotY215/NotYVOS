#include <kernel/block/block.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/usb/msc.hpp>
#include <kernel/usb/xhci.hpp>

namespace notyvos::usb::msc
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;
constexpr u32 kMaxDevices = 4;
constexpr u32 kScsiTimeout = 4000000u;

// USB class codes for mass storage.
constexpr u8 kClassMsc          = 8;
constexpr u8 kSubclassScsi      = 6;
constexpr u8 kProtocolBot       = 0x50;

// BOT CBW/CSW.
struct Cbw
{
    u32 dCbwSignature;   // 0x43425355 = "USBC"
    u32 dCbwTag;
    u32 dCbwDataTransferLength;
    u8  bmCbwFlags;      // 0x80 = IN, 0x00 = OUT
    u8  bCbwLun;
    u8  bCbwCBLength;
    u8  CBWCB[16];
} __attribute__((packed));

struct Csw
{
    u32 dCswSignature;   // 0x53425355 = "USBS"
    u32 dCswTag;
    u32 dCswDataResidue;
    u8  bCswStatus;      // 0 = pass, 1 = fail, 2 = phase error
} __attribute__((packed));

static_assert(sizeof(Cbw) == 31, "CBW must be 31 bytes");
static_assert(sizeof(Csw) == 13, "CSW must be 13 bytes");

struct MscDevice
{
    bool  present;
    u32   ci;
    u8    slot_id;
    u8    ep_in;
    u8    ep_out;
    u16   ep_in_max;
    u16   ep_out_max;
    u32   tag;
    u32   block_count;
    u16   block_size;
    char  vendor[9];
    char  product[17];

    // Scratch transfer buffer (HHDM-mapped). One 4 KB page.
    u64   bounce_phys;
    u8*   bounce_virt;
};

MscDevice g_devices[kMaxDevices];
u32       g_count = 0;

// USB class-specific interface descriptor header.
struct ConfigDescriptor
{
    u8  bLength;
    u8  bDescriptorType;
    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} __attribute__((packed));

struct InterfaceDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} __attribute__((packed));

struct EndpointDescriptor
{
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} __attribute__((packed));

// Locate MSC interface and bulk endpoints.
bool find_msc_interface(const u8* cfg, u16 cfg_len,
                        u8& ep_in, u8& ep_out,
                        u16& ep_in_max, u16& ep_out_max) noexcept
{
    u16 off = 0;
    bool in_msc = false;

    while (off + 2 <= cfg_len)
    {
        const u8 bLen = cfg[off];
        const u8 bType = cfg[off + 1];
        if (bLen == 0) break;
        if (off + bLen > cfg_len) break;

        if (bType == 4 && bLen >= 9)
        {
            const auto* id = reinterpret_cast<const InterfaceDescriptor*>(cfg + off);
            in_msc = (id->bInterfaceClass == kClassMsc &&
                      id->bInterfaceSubClass == kSubclassScsi &&
                      id->bInterfaceProtocol == kProtocolBot);
        }
        else if (bType == 5 && bLen >= 7 && in_msc)
        {
            const auto* ed = reinterpret_cast<const EndpointDescriptor*>(cfg + off);
            const u8 attr = ed->bmAttributes & 0x03u;
            if (attr != 2u) { off += bLen; continue; }

            const bool is_in = (ed->bEndpointAddress & 0x80u) != 0;
            if (is_in)
            {
                ep_in = static_cast<u8>(ed->bEndpointAddress & 0x0Fu);
                ep_in_max = static_cast<u16>(ed->wMaxPacketSize & 0x07FFu);
            }
            else
            {
                ep_out = static_cast<u8>(ed->bEndpointAddress & 0x0Fu);
                ep_out_max = static_cast<u16>(ed->wMaxPacketSize & 0x07FFu);
            }

            if (ep_in && ep_out) return true;
        }
        off = static_cast<u16>(off + bLen);
    }
    return false;
}

// Send CBW, data phase, CSW. Returns true on success.
bool bot_transfer(MscDevice& d,
                  const u8* cb, u8 cb_len,
                  void* data, u32 data_len,
                  bool data_in) noexcept
{
    if (data_len > 4096) return false;

    // ---- CBW ----
    Cbw cbw{};
    cbw.dCbwSignature = 0x43425355u;   // "USBC"
    cbw.dCbwTag = ++d.tag;
    cbw.dCbwDataTransferLength = data_len;
    cbw.bmCbwFlags = data_in ? 0x80u : 0x00u;
    cbw.bCbwLun = 0;
    cbw.bCbwCBLength = cb_len;
    for (u32 i = 0; i < cb_len && i < 16; ++i) cbw.CBWCB[i] = cb[i];

    // Send CBW via bulk OUT.
    libk::memcpy(d.bounce_virt, &cbw, sizeof(cbw));
    isize sent = xhci::bulk_transfer(d.ci, d.slot_id, d.ep_out,
                                     xhci::EpDir::Out,
                                     d.bounce_virt, sizeof(cbw));
    if (sent != static_cast<isize>(sizeof(cbw))) return false;

    // ---- Data phase ----
    if (data_len > 0)
    {
        if (data_in)
        {
            libk::memset(d.bounce_virt, 0, data_len);
            const isize got = xhci::bulk_transfer(d.ci, d.slot_id, d.ep_in,
                                                   xhci::EpDir::In,
                                                   d.bounce_virt, data_len);
            if (got <= 0) return false;
            libk::memcpy(data, d.bounce_virt, static_cast<usize>(got));
        }
        else
        {
            libk::memcpy(d.bounce_virt, data, data_len);
            const isize put = xhci::bulk_transfer(d.ci, d.slot_id, d.ep_out,
                                                   xhci::EpDir::Out,
                                                   d.bounce_virt, data_len);
            if (put != static_cast<isize>(data_len)) return false;
        }
    }

    // ---- CSW ----
    Csw csw{};
    const isize got_csw = xhci::bulk_transfer(d.ci, d.slot_id, d.ep_in,
                                               xhci::EpDir::In,
                                               d.bounce_virt, sizeof(Csw));
    if (got_csw != static_cast<isize>(sizeof(Csw))) return false;
    libk::memcpy(&csw, d.bounce_virt, sizeof(csw));

    if (csw.dCswSignature != 0x53425355u) return false;
    if (csw.bCswStatus != 0) return false;
    (void)kScsiTimeout;
    return true;
}

// ---- SCSI commands ----

bool scsi_inquiry(MscDevice& d) noexcept
{
    u8 cb[6] = { 0x12, 0, 0, 0, 36, 0 };   // INQUIRY, alloc len 36
    alignas(64) u8 buf[36] = {0};
    if (!bot_transfer(d, cb, 6, buf, 36, true)) return false;

    // Vendor is at offset 8, product at 16.
    for (u32 i = 0; i < 8; ++i)
    {
        const char c = static_cast<char>(buf[8 + i]);
        d.vendor[i] = (c >= 0x20 && c <= 0x7E) ? c : ' ';
    }
    d.vendor[8] = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        const char c = static_cast<char>(buf[16 + i]);
        d.product[i] = (c >= 0x20 && c <= 0x7E) ? c : ' ';
    }
    d.product[16] = 0;

    log::write(log::Level::Info, "msc", "INQUIRY: vendor='%s' product='%s'",
               d.vendor, d.product);
    return true;
}

bool scsi_read_capacity(MscDevice& d) noexcept
{
    u8 cb[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // READ_CAPACITY(10)
    alignas(64) u8 buf[8] = {0};
    if (!bot_transfer(d, cb, 10, buf, 8, true)) return false;

    d.block_count = (static_cast<u32>(buf[0]) << 24) | (static_cast<u32>(buf[1]) << 16) |
                    (static_cast<u32>(buf[2]) << 8) | static_cast<u32>(buf[3]);
    d.block_size = static_cast<u16>((static_cast<u16>(buf[4]) << 8) | static_cast<u16>(buf[5]));

    // block_count is "last LBA", so total blocks = that + 1.
    d.block_count += 1u;

    log::write(log::Level::Info, "msc",
               "READ CAPACITY: %llu blocks of %llu bytes",
               static_cast<unsigned long long>(d.block_count),
               static_cast<unsigned long long>(d.block_size));
    return true;
}

isize scsi_read10(MscDevice& d, u64 lba, u32 count, void* buf) noexcept
{
    if (count > 8) return -1;   // one page worth
    u8 cb[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // READ(10)
    cb[2] = static_cast<u8>((lba >> 24) & 0xFFu);
    cb[3] = static_cast<u8>((lba >> 16) & 0xFFu);
    cb[4] = static_cast<u8>((lba >>  8) & 0xFFu);
    cb[5] = static_cast<u8>(lba & 0xFFu);
    cb[7] = static_cast<u8>((count >> 8) & 0xFFu);
    cb[8] = static_cast<u8>(count & 0xFFu);
    const u32 bytes = count * d.block_size;
    if (!bot_transfer(d, cb, 10, buf, bytes, true)) return -1;
    return static_cast<isize>(bytes);
}

isize scsi_write10(MscDevice& d, u64 lba, u32 count, const void* buf) noexcept
{
    if (count > 8) return -1;
    u8 cb[10] = { 0x2A, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // WRITE(10)
    cb[2] = static_cast<u8>((lba >> 24) & 0xFFu);
    cb[3] = static_cast<u8>((lba >> 16) & 0xFFu);
    cb[4] = static_cast<u8>((lba >>  8) & 0xFFu);
    cb[5] = static_cast<u8>(lba & 0xFFu);
    cb[7] = static_cast<u8>((count >> 8) & 0xFFu);
    cb[8] = static_cast<u8>(count & 0xFFu);
    const u32 bytes = count * d.block_size;
    if (!bot_transfer(d, cb, 10, const_cast<void*>(buf), bytes, false)) return -1;
    return static_cast<isize>(bytes);
}

// ---- Block device shim ----
isize block_read(block::BlockDevice* dev, u64 lba, u32 count, void* buf) noexcept
{
    auto* d = static_cast<MscDevice*>(dev->driver_data);
    if (!d) return -1;
    if (count == 0) return 0;
    if (count > 8) return -1;

    const usize bytes = static_cast<usize>(count) * 512u;
    // Read in multiples of the device's block size (usually 512).
    const isize got = scsi_read10(*d, lba, count, buf);
    if (got < 0) return -1;
    return static_cast<isize>(bytes);
}

isize block_write(block::BlockDevice* dev, u64 lba, u32 count, const void* buf) noexcept
{
    auto* d = static_cast<MscDevice*>(dev->driver_data);
    if (!d) return -1;
    if (count == 0) return 0;
    if (count > 8) return -1;

    const usize bytes = static_cast<usize>(count) * 512u;
    const isize put = scsi_write10(*d, lba, count, buf);
    if (put < 0) return -1;
    return static_cast<isize>(bytes);
}

void attach(MscDevice& d) noexcept
{
    if (d.block_size == 0) d.block_size = 512;

    auto* bd = static_cast<block::BlockDevice*>(
        mm::Heap::allocate(sizeof(block::BlockDevice)));
    if (!bd) return;

    libk::memset(bd, 0, sizeof(*bd));
    const u32 n = g_count;
    // Name: "usb0", "usb1", ...
    bd->name[0] = 'u'; bd->name[1] = 's'; bd->name[2] = 'b';
    bd->name[3] = static_cast<char>('0' + (n % 10u));
    bd->name[4] = 0;

    bd->sector_count = d.block_count;
    bd->logical_sector_size = 512;   // we expose 512-byte sectors
    bd->driver_data = &d;
    bd->read_sectors = block_read;
    bd->write_sectors = block_write;
    bd->read_only = false;

    (void)block::block_register(bd);

    log::write(log::Level::Info, "msc",
               "registered %s: %llu blocks of %llu bytes",
               bd->name,
               static_cast<unsigned long long>(d.block_count),
               static_cast<unsigned long long>(d.block_size));
}

void bring_up(u32 ci, u8 slot_id, u8 ep_in, u8 ep_out,
              u16 ep_in_max, u16 ep_out_max) noexcept
{
    if (g_count >= kMaxDevices) return;

    MscDevice& d = g_devices[g_count];
    d.present     = true;
    d.ci          = ci;
    d.slot_id     = slot_id;
    d.ep_in       = ep_in;
    d.ep_out      = ep_out;
    d.ep_in_max   = ep_in_max;
    d.ep_out_max  = ep_out_max;
    d.tag         = 0;
    d.block_count = 0;
    d.block_size  = 0;

    // Bounce buffer: one DMA page.
    d.bounce_phys = static_cast<u64>(mm::PhysicalMemory::allocate_frame());
    if (d.bounce_phys == 0) { d.present = false; return; }
    d.bounce_virt = reinterpret_cast<u8*>(d.bounce_phys + kHhdm);
    libk::memset(d.bounce_virt, 0, 4096);

    if (!xhci::set_configuration(ci, slot_id, 1))
    {
        log::write(log::Level::Warn, "msc",
                   "slot %llu: SET_CONFIGURATION failed",
                   static_cast<unsigned long long>(slot_id));
        d.present = false;
        return;
    }

    if (!xhci::configure_endpoint(ci, slot_id, ep_in,
                                  xhci::EpDir::In, xhci::EpType::Bulk,
                                  ep_in_max, 0) ||
        !xhci::configure_endpoint(ci, slot_id, ep_out,
                                  xhci::EpDir::Out, xhci::EpType::Bulk,
                                  ep_out_max, 0))
    {
        log::write(log::Level::Warn, "msc",
                   "slot %llu: bulk endpoint setup failed",
                   static_cast<unsigned long long>(slot_id));
        d.present = false;
        return;
    }

    if (!scsi_inquiry(d))
    {
        log::write(log::Level::Warn, "msc", "INQUIRY failed");
        d.present = false;
        return;
    }
    if (!scsi_read_capacity(d))
    {
        log::write(log::Level::Warn, "msc", "READ CAPACITY failed");
        d.present = false;
        return;
    }

    ++g_count;
    attach(d);
}

} // namespace

void init() noexcept
{
    g_count = 0;
    libk::memset(g_devices, 0, sizeof(g_devices));

    const u32 n_ctl = xhci::controller_count();
    if (n_ctl == 0) return;

    const u32 n_dev = xhci::device_count();
    for (u32 i = 0; i < n_dev; ++i)
    {
        const auto* dev = xhci::device(i);
        if (!dev) continue;

        alignas(64) u8 cfg_buf[256];
        libk::memset(cfg_buf, 0, sizeof(cfg_buf));

        const isize got_hdr = xhci::get_descriptor(dev->controller_index,
                                                    dev->slot_id,
                                                    2, 0, 0, cfg_buf, 9);
        if (got_hdr < 9) continue;

        const auto* cfg = reinterpret_cast<const ConfigDescriptor*>(cfg_buf);
        const u16 total = cfg->wTotalLength;
        if (total > sizeof(cfg_buf)) continue;

        const isize got_full = xhci::get_descriptor(dev->controller_index,
                                                     dev->slot_id,
                                                     2, 0, 0, cfg_buf, total);
        if (got_full < 9) continue;

        u8  ep_in = 0, ep_out = 0;
        u16 ep_in_max = 0, ep_out_max = 0;
        if (!find_msc_interface(cfg_buf, total,
                                ep_in, ep_out, ep_in_max, ep_out_max))
            continue;

        bring_up(dev->controller_index, dev->slot_id,
                 ep_in, ep_out, ep_in_max, ep_out_max);
    }

    log::write(log::Level::Info, "msc",
               "MSC init: %llu device(s)",
               static_cast<unsigned long long>(g_count));
}

u32 devices() noexcept { return g_count; }

} // namespace notyvos::usb::msc
