#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/bt/bt.hpp>
#include <kernel/bt/hci.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

// Defined in kernel/src/bt/bt_transport.cpp.
extern "C" void bt_store_transport(notyvos::bt::HciSendFn fn, void* user) noexcept;

namespace notyvos::bt
{

namespace
{

Device g_devices[kMaxDevices];
u32 g_device_count = 0;

Transport g_transport = Transport::None;
HciSendFn g_send = nullptr;
void* g_send_user = nullptr;
bool g_powered = false;

bool g_inquiry = false;
u64 g_inquiry_started = 0;
u32 g_inquiry_timeout_ms = 0;

u64 g_inquiries = 0;
u64 g_devices_found_total = 0;

Device* find_by_addr(const Address& a) noexcept
{
    for (u32 i = 0; i < g_device_count; ++i)
        if (libk::memcmp(g_devices[i].address.b, a.b, 6) == 0)
            return &g_devices[i];
    return nullptr;
}

Device* alloc_device(const Address& a) noexcept
{
    Device* d = find_by_addr(a);
    if (d)
        return d;
    if (g_device_count >= kMaxDevices)
        return nullptr;
    Device& nd = g_devices[g_device_count++];
    libk::memset(&nd, 0, sizeof(nd));
    libk::memcpy(nd.address.b, a.b, 6);
    nd.cls = DeviceClass::Unknown;
    nd.rssi = -100;
    nd.le = false;
    ++g_devices_found_total;
    return &nd;
}

} // namespace

void init() noexcept
{
    g_device_count = 0;
    g_transport = Transport::None;
    g_send = nullptr;
    g_send_user = nullptr;
    g_powered = false;
    g_inquiry = false;
    g_inquiries = 0;
    g_devices_found_total = 0;
    libk::memset(g_devices, 0, sizeof(g_devices));
    log::write(log::Level::Info, "bt", "Bluetooth framework ready");
}

u32 device_count() noexcept
{
    return g_device_count;
}

const Device* device(u32 index) noexcept
{
    return (index < g_device_count) ? &g_devices[index] : nullptr;
}

void register_transport(Transport t, HciSendFn send, void* user) noexcept
{
    g_transport = t;
    g_send = send;
    g_send_user = user;
    bt_store_transport(send, user);
    log::write(log::Level::Info, "bt", "transport registered (type=%u)", static_cast<u64>(t));
}

Transport current_transport() noexcept
{
    return g_transport;
}
bool powered() noexcept
{
    return g_powered;
}

bool power_on() noexcept
{
    if (!g_send)
    {
        log::write(log::Level::Warn, "bt", "no transport");
        return false;
    }
    if (!hci::send_command(static_cast<u16>((hci::kOgfControllerBase << 10) | hci::kOcfReset),
                           nullptr, 0))
        return false;
    g_powered = true;
    log::write(log::Level::Info, "bt", "controller powered on");
    return true;
}

void power_off() noexcept
{
    g_powered = false;
    g_inquiry = false;
    log::write(log::Level::Info, "bt", "controller powered off");
}

bool start_inquiry(u32 timeout_ms) noexcept
{
    if (!g_powered || !g_send)
        return false;
    u8 params[5];
    params[0] = 0x33;
    params[1] = 0x8B;
    params[2] = 0x9E;
    params[3] = 0x30;
    params[4] = 0x00;
    if (!hci::send_command(static_cast<u16>((hci::kOgfLinkControl << 10) | hci::kOcfInquiry),
                           params, 5))
        return false;
    g_inquiry = true;
    g_inquiry_started = arch::x86_64::pit_ticks();
    g_inquiry_timeout_ms = timeout_ms;
    ++g_inquiries;
    log::write(log::Level::Info, "bt", "inquiry started (%u ms)", timeout_ms);
    return true;
}

void stop_inquiry() noexcept
{
    if (!g_inquiry)
        return;
    (void)hci::send_command(static_cast<u16>((hci::kOgfLinkControl << 10) | hci::kOcfInquiryCancel),
                            nullptr, 0);
    g_inquiry = false;
    log::write(log::Level::Info, "bt", "inquiry stopped");
}

bool inquiry_active() noexcept
{
    return g_inquiry;
}

bool pair(const Address& addr) noexcept
{
    Device* d = find_by_addr(addr);
    if (!d)
        return false;
    d->paired = true;
    log::write(log::Level::Info, "bt", "device paired");
    return true;
}

bool connect(const Address& addr) noexcept
{
    Device* d = find_by_addr(addr);
    if (!d)
        return false;
    u8 params[13];
    libk::memcpy(params, addr.b, 6);
    params[6] = 0x18;
    params[7] = 0xCC;
    params[8] = 0x01;
    params[9] = 0x00;
    params[10] = 0x00;
    params[11] = 0x00;
    params[12] = 0x01;
    (void)hci::send_command(static_cast<u16>((hci::kOgfLinkControl << 10) | hci::kOcfCreateConn),
                            params, 13);
    return true;
}

void disconnect(const Address& addr) noexcept
{
    Device* d = find_by_addr(addr);
    if (!d)
        return;
    u8 params[7];
    libk::memcpy(params, addr.b, 6);
    params[6] = 0x13;
    (void)hci::send_command(static_cast<u16>((hci::kOgfLinkControl << 10) | hci::kOcfDisconnect),
                            params, 7);
}

void hci_receive(const u8* data, usize len) noexcept
{
    if (!data || len < 1)
        return;
    const u8 type = data[0];
    if (type == hci::kPktEvent)
    {
        hci::handle_event(data + 1, len - 1);
    }
    else if (type == hci::kPktAcl)
    {
        hci::handle_acl(data + 1, len - 1);
    }
}

void tick() noexcept
{
    if (!g_inquiry)
        return;
    const u64 now = arch::x86_64::pit_ticks();
    if (now - g_inquiry_started > (static_cast<u64>(g_inquiry_timeout_ms) * 100ULL) / 1000ULL)
    {
        stop_inquiry();
    }
}

u64 inquiries_started() noexcept
{
    return g_inquiries;
}
u64 devices_found() noexcept
{
    return g_devices_found_total;
}

void format_address(const Address& a, char* out) noexcept
{
    const char* hex = "0123456789abcdef";
    u32 p = 0;
    for (u32 i = 0; i < 6; ++i)
    {
        if (i > 0)
            out[p++] = ':';
        out[p++] = hex[(a.b[i] >> 4) & 0xF];
        out[p++] = hex[a.b[i] & 0xF];
    }
    out[p] = 0;
}

void on_inquiry_result(const Address& addr, u8 dev_class_major, i8 rssi) noexcept
{
    Device* d = alloc_device(addr);
    if (!d)
        return;
    if (dev_class_major == 0x01)
        d->cls = DeviceClass::Computer;
    else if (dev_class_major == 0x02)
        d->cls = DeviceClass::Phone;
    else if (dev_class_major == 0x04)
        d->cls = DeviceClass::Audio;
    else if (dev_class_major == 0x05)
        d->cls = DeviceClass::Other;
    else
        d->cls = DeviceClass::Unknown;
    d->rssi = rssi;

    if (d->name[0] == 0)
    {
        const char* base = "Device";
        if (d->cls == DeviceClass::Computer)
            base = "Computer";
        else if (d->cls == DeviceClass::Phone)
            base = "Phone";
        else if (d->cls == DeviceClass::Audio)
            base = "Audio";
        else if (d->cls == DeviceClass::Headset)
            base = "Headset";
        u32 i = 0;
        while (base[i] && i < kMaxNameLen - 1)
        {
            d->name[i] = base[i];
            ++i;
        }
        d->name[i] = 0;
    }
}

void on_connection_complete(const Address& addr, bool ok) noexcept
{
    Device* d = find_by_addr(addr);
    if (!d)
        return;
    d->connected = ok;
    log::write(log::Level::Info, "bt", "connection %s", ok ? "complete" : "failed");
}

} // namespace notyvos::bt
