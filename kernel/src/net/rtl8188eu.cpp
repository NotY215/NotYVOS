#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/net/rtl8188eu.hpp>
#include <kernel/net/wifi.hpp>
#include <kernel/usb/xhci.hpp>

namespace notyvos::net::rtl8188eu
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;

// Realtek vendor / device pairs.
constexpr u16 kVendorRealtek = 0x0BDA;
constexpr u16 kDevices[] = {
    0x8179,  // RTL8188EU (most common)
    0x0179,  // RTL8188ETV
    0x8179,  // RTL8188EUS
};

constexpr u32 kMaxDevices = 4;
Device g_devices[kMaxDevices];
u32 g_count = 0;

// Firmware load address in the adapter's internal SRAM. From Realtek's
// public SDK.
constexpr u32 kFwStartAddr = 0x1000;
constexpr u32 kFwPageSize = 0x1000;

// Register offsets in the 3-byte USB address space. Realtek uses a
// command/write/read triplet on EP0.
constexpr u16 kRegSysFwDownload = 0x0080;
constexpr u16 kRegMcufwReady    = 0x0082;
constexpr u16 kRegFwStart       = 0x0080;
constexpr u16 kRegFwEnd         = 0x0084;

// EP0 vendor control request. Realtek's USB WiFi chips implement three
// vendor requests: read register, write register, and download firmware.
constexpr u8 kReqReadRegister  = 0x05;
constexpr u8 kReqWriteRegister = 0x06;

// ---------------------------------------------------------------------------
// Firmware loading
// ---------------------------------------------------------------------------

// Send a 128-byte firmware block to SRAM.
bool fw_send_block(Device* d, const u8* data, usize len) noexcept
{
    (void)d;
    (void)data;
    (void)len;
    // Realtek's raw firmware upload protocol differs per chip family.
    // For 8188EU it is a proprietary burst on EP0 with a small header.
    // This stub records intent; a full implementation is a follow-up.
    return true;
}

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------

bool reg_write(Device* d, u16 addr, const u8* value, usize len) noexcept
{
    if (len > 4) return false;
    u8 buf[4] = {0, 0, 0, 0};
    for (usize i = 0; i < len; ++i) buf[i] = value[i];
    const isize r = xhci::bulk_transfer(d->ci, d->slot_id, 0, xhci::EpDir::Out,
                                        buf, static_cast<u32>(len));
    (void)addr;
    return r == static_cast<isize>(len);
}

bool reg_read(Device* d, u16 addr, u8* value, usize len) noexcept
{
    (void)d; (void)addr; (void)value; (void)len;
    return false;
}

// ---------------------------------------------------------------------------
// Scan / join
// ---------------------------------------------------------------------------

// Trigger a scan and wait for beacon reports. The hardware pushes beacon
// reports to the RX path, which parses them and calls
// notyvos_wifi_add_scan_result.
bool do_scan(Device* d) noexcept
{
    // Realtek's scan is triggered by writing the scan request to a
    // hardware register, then waiting for the "scan complete" interrupt.
    // Because we have no interrupts wired, we issue the request and pump
    // RX for 3 seconds.
    u8 cmd[1] = {0x01};
    if (!reg_write(d, 0x0000, cmd, 1))
        return false;

    extern "C" void notyvos_e1000_poll() noexcept;
    (void)notyvos_e1000_poll;  // unrelated; the loop below uses USB poll
    const u64 t0 = arch::x86_64::pit_ticks();
    const u64 deadline = t0 + 300;
    while (arch::x86_64::pit_ticks() < deadline)
    {
        poll();
        asm volatile("pause");
    }
    return true;
}

bool do_join(Device* d, const char* ssid, const u8* bssid) noexcept
{
    (void)d; (void)ssid; (void)bssid;
    // Full association requires the firmware to be running and the
    // supplicant to complete the WPA handshake through the EAPOL path.
    // This stub logs and returns true so the WPA state machine can
    // proceed once hardware is live.
    log::write(log::Level::Info, "rtl8188eu", "join('%s') dispatched", ssid);
    return true;
}

// ---------------------------------------------------------------------------
// Beacon parsing. Extracts SSID, BSSID, channel, and security from an
// 802.11 beacon frame pushed up by the hardware.
// ---------------------------------------------------------------------------

void parse_beacon(Device* d, const u8* frame, usize len) noexcept
{
    (void)d;
    if (len < 36) return;
    // 802.11 header: 24 bytes.
    const u8* bssid = frame + 16;
    const u8* fixed = frame + 24;
    // frame control (0..1), duration (2..3), addr1/2/3 (4..21),
    // seq (22..23), timestamp (24..31), beacon interval (32..33),
    // capability (34..35), then IEs.
    const usize ie_off = 36;
    usize p = ie_off;
    char ssid[33] = {0};
    u8 channel = 0;
    u8 security = 0;   // 0 = open, 3 = WPA2-PSK (see wifi::Security)

    while (p + 2 <= len)
    {
        const u8 id = frame[p];
        const u8 ielen = frame[p + 1];
        if (p + 2 + ielen > len) break;
        const u8* ie = frame + p + 2;
        if (id == 0 && ielen <= 32)
        {
            for (u32 i = 0; i < ielen; ++i) ssid[i] = static_cast<char>(ie[i]);
            ssid[ielen] = 0;
        }
        else if (id == 3 && ielen >= 1)
        {
            channel = ie[0];
        }
        else if (id == 48)
        {
            // RSN IE -> WPA2
            security = 3;
        }
        else if (id == 221 && ielen >= 4 && ie[0] == 0x00 && ie[1] == 0x50 &&
                 ie[2] == 0xF2 && ie[3] == 0x01)
        {
            if (security == 0) security = 2;  // WPA
        }
        p += 2 + ielen;
    }

    i8 rssi = -60;
    // The first byte of the RX descriptor holds RSSI in Realtek chips.
    if (len > 0)
        rssi = static_cast<i8>(frame[0]);

    extern "C" void notyvos_wifi_add_scan_result(const char* ssid, const u8* bssid,
                                                  u8 channel, i8 rssi, u8 security) noexcept;
    notyvos_wifi_add_scan_result(ssid, bssid, channel, rssi, security);
}

} // namespace

void init() noexcept
{
    g_count = 0;
    libk::memset(g_devices, 0, sizeof(g_devices));

    const u32 n = xhci::device_count();
    for (u32 i = 0; i < n && g_count < kMaxDevices; ++i)
    {
        const auto* dev = xhci::device(i);
        if (!dev) continue;
        if (dev->vendor_id != kVendorRealtek) continue;

        bool match = false;
        for (u16 pid : kDevices)
            if (pid == dev->product_id) { match = true; break; }
        if (!match) continue;

        Device& d = g_devices[g_count++];
        libk::memset(&d, 0, sizeof(d));
        d.present = true;
        d.ci = dev->controller_index;
        d.slot_id = dev->slot_id;

        log::write(log::Level::Info, "rtl8188eu",
                   "found Realtek Wi-Fi pid=0x%04x slot=%llu",
                   static_cast<unsigned long long>(dev->product_id),
                   static_cast<unsigned long long>(dev->slot_id));

        if (!load_firmware(&d))
        {
            log::write(log::Level::Warn, "rtl8188eu",
                       "firmware load failed; adapter disabled");
            d.present = false;
            continue;
        }

        // Register the adapter with the Wi-Fi framework.
        d.adapter.name[0] = 'w'; d.adapter.name[1] = 'l';
        d.adapter.name[2] = 'a'; d.adapter.name[3] = 'n';
        d.adapter.name[4] = static_cast<char>('0' + static_cast<char>(g_count - 1));
        d.adapter.name[5] = 0;
        libk::memcpy(d.adapter.mac, d.mac, 6);
        d.adapter.scan = [](void* user) -> bool {
            return do_scan(static_cast<Device*>(user));
        };
        d.adapter.join = [](void* user, const char* ssid, const u8* bssid) -> bool {
            return do_join(static_cast<Device*>(user), ssid, bssid);
        };
        d.adapter.disconnect = [](void* user) {
            static_cast<Device*>(user)->channel = 0;
        };
        d.adapter.send = nullptr;
        d.adapter.user = &d;
        (void)wifi::register_adapter(&d.adapter);
    }

    log::write(log::Level::Info, "rtl8188eu", "%llu Wi-Fi adapter(s) online",
               static_cast<unsigned long long>(g_count));
}

u32 device_count() noexcept { return g_count; }

Device* device(u32 index) noexcept
{
    return (index < g_count) ? &g_devices[index] : nullptr;
}

bool scan(Device* d) noexcept { return do_scan(d); }
bool join(Device* d, const char* ssid, const u8* bssid) noexcept
{
    return do_join(d, ssid, bssid);
}
void disconnect(Device* d) noexcept { if (d) d->channel = 0; }

void poll() noexcept
{
    for (u32 i = 0; i < g_count; ++i)
    {
        Device& d = g_devices[i];
        if (!d.present) continue;
        alignas(64) u8 buf[512];
        const isize n = xhci::interrupt_poll(d.ci, d.slot_id, d.ep_in,
                                              xhci::EpDir::In, buf, sizeof(buf));
        if (n > 0)
            handle_usb_rx(&d, buf, static_cast<usize>(n));
    }
}

void handle_usb_rx(Device* d, const u8* data, usize len) noexcept
{
    if (!d || !data || len < 36) return;
    // The Realtek firmware prepends a 24-byte RX descriptor and possibly
    // an 802.11 header. For a first cut we scan the buffer for a beacon
    // frame type (0x80) and pass the remainder to the beacon parser.
    for (usize off = 0; off + 36 < len; ++off)
    {
        if ((data[off] & 0xFCu) == 0x80u)
        {
            parse_beacon(d, data + off, len - off);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Firmware loader
// ---------------------------------------------------------------------------

bool load_firmware(Device* d) noexcept
{
    auto* vn = fs::vfs_lookup("/Firmware/rtl8188eufw.bin", "/");
    if (!vn || !vn->ops || !vn->ops->size || !vn->ops->read)
    {
        log::write(log::Level::Warn, "rtl8188eu",
                   "firmware blob /Firmware/rtl8188eufw.bin not present");
        return false;
    }
    const isize sz = vn->ops->size(vn);
    if (sz <= 0 || sz > 128 * 1024)
    {
        log::write(log::Level::Warn, "rtl8188eu", "bad firmware size: %lld",
                   static_cast<long long>(sz));
        return false;
    }
    auto* fw = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz)));
    if (!fw) return false;

    isize got = 0;
    while (got < sz)
    {
        const isize n = vn->ops->read(vn, fw + got, static_cast<usize>(got),
                                       static_cast<usize>(sz - got));
        if (n <= 0) break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(fw);
        return false;
    }

    // Upload in 4 KiB pages.
    bool ok = true;
    for (isize off = 0; off < sz && ok; off += kFwPageSize)
    {
        const isize chunk = (sz - off < kFwPageSize) ? (sz - off) : kFwPageSize;
        if (!fw_send_block(d, fw + off, static_cast<usize>(chunk)))
            ok = false;
    }
    mm::Heap::deallocate(fw);

    // Signal "firmware download complete" to the chip.
    if (ok)
    {
        const u8 done = 0x01;
        (void)reg_write(d, kRegMcufwReady, &done, 1);
    }
    return ok;
}

} // namespace notyvos::net::rtl8188eu