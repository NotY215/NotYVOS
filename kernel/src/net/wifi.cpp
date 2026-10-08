#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/wifi.hpp>

namespace notyvos::net::wifi
{

namespace
{
Adapter* g_adapters[kMaxIfaces] = {};
u32 g_count = 0;

ScanResult g_last_scan[kMaxScanResults] = {};
u32 g_last_scan_count = 0;
char g_last_scan_adapter[kIfNameMax] = {};
} // namespace

void init() noexcept
{
    g_count = 0;
    for (u32 i = 0; i < kMaxIfaces; ++i)
        g_adapters[i] = nullptr;
    log::write(log::Level::Info, "wifi", "Wi-Fi framework ready");
}

bool register_adapter(Adapter* a) noexcept
{
    if (!a || g_count >= kMaxIfaces)
        return false;
    g_adapters[g_count++] = a;
    log::write(log::Level::Info, "wifi", "adapter %s registered", a->name);
    return true;
}

u32 adapter_count() noexcept
{
    return g_count;
}

Adapter* adapter_by_index(u32 i) noexcept
{
    return (i < g_count) ? g_adapters[i] : nullptr;
}

Adapter* adapter_by_name(const char* name) noexcept
{
    if (!name)
        return nullptr;
    for (u32 i = 0; i < g_count; ++i)
        if (libk::strcmp(g_adapters[i]->name, name) == 0)
            return g_adapters[i];
    return nullptr;
}

bool scan(const char* adapter_name) noexcept
{
    Adapter* a = adapter_by_name(adapter_name);
    if (!a || !a->scan)
        return false;
    g_last_scan_count = 0;
    u32 n = 0;
    while (n < kIfNameMax - 1 && adapter_name[n])
    {
        g_last_scan_adapter[n] = adapter_name[n];
        ++n;
    }
    g_last_scan_adapter[n] = 0;
    return a->scan(a->user);
}

u32 scan_results(const char* adapter_name, ScanResult* out, u32 max) noexcept
{
    if (!adapter_name || !out)
        return 0;
    if (libk::strcmp(adapter_name, g_last_scan_adapter) != 0)
        return 0;
    const u32 n = (g_last_scan_count < max) ? g_last_scan_count : max;
    for (u32 i = 0; i < n; ++i)
        out[i] = g_last_scan[i];
    return n;
}

bool associate(const char* adapter_name, const char* ssid) noexcept
{
    Adapter* a = adapter_by_name(adapter_name);
    if (!a || !a->join || !ssid)
        return false;
    // BSSID is chosen by the driver; pass null to let it decide.
    return a->join(a->user, ssid, nullptr);
}

void disconnect(const char* adapter_name) noexcept
{
    Adapter* a = adapter_by_name(adapter_name);
    if (!a || !a->disconnect)
        return;
    a->disconnect(a->user);
    a->connected = false;
    a->current_ssid[0] = 0;
}

void deliver_rx(Adapter* a, const u8* frame, usize len) noexcept
{
    if (!a || !frame || len == 0)
        return;
    // Wi-Fi frames carry a 802.11 header. For now we hand the whole frame
    // to the Ethernet layer under the assumption the driver has already
    // stripped 802.11 and produced an 802.3 frame. Drivers that need
    // 802.11 translation do it before calling this.
    receive_frame(iface_by_name(a->name), frame, len);
}

void notify_associated(Adapter* a, bool ok) noexcept
{
    if (!a)
        return;
    a->connected = ok;
    log::write(log::Level::Info, "wifi", "adapter %s association %s", a->name,
               ok ? "ok" : "failed");
}

// Driver helper: push a scan result up into the cache.
extern "C" void notyvos_wifi_add_scan_result(const char* ssid, const u8* bssid, u8 channel,
                                              i8 rssi, u8 security) noexcept
{
    if (g_last_scan_count >= kMaxScanResults)
        return;
    ScanResult& r = g_last_scan[g_last_scan_count++];
    u32 i = 0;
    while (ssid[i] && i < kMaxSsid)
    {
        r.ssid[i] = ssid[i];
        ++i;
    }
    r.ssid[i] = 0;
    for (u32 k = 0; k < 6; ++k)
        r.bssid[k] = bssid[k];
    r.channel = channel;
    r.signal_dbm = rssi;
    r.security = static_cast<Security>(security);
}

} // namespace notyvos::net::wifi