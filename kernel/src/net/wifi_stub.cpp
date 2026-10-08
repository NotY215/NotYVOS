#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/net/wifi.hpp>

namespace notyvos::net::wifi_stub
{

namespace
{

wifi::Adapter g_adapter;

// Canned scan results. Add or remove entries here.
struct StubAp
{
    const char* ssid;
    u8          bssid[6];
    u8          channel;
    i8          rssi;
    u8          security;   // matches wifi::Security enum values
};

const StubAp g_aps[] = {
    {"HomeWifi",        {0x10,0x20,0x30,0x40,0x50,0x01},  6, -45, 3},
    {"OfficeNet",       {0x10,0x20,0x30,0x40,0x50,0x02},  1, -60, 3},
    {"CoffeeShop_Guest",{0x10,0x20,0x30,0x40,0x50,0x03}, 11, -72, 0},
    {"Neighbour_5G",    {0x10,0x20,0x30,0x40,0x50,0x04}, 36, -80, 3},
    {"IoT_Devices",     {0x10,0x20,0x30,0x40,0x50,0x05},  9, -65, 2},
};
constexpr u32 kApCount = sizeof(g_aps) / sizeof(g_aps[0]);

extern "C" void notyvos_wifi_add_scan_result(const char* ssid, const u8* bssid,
                                              u8 channel, i8 rssi, u8 security) noexcept;

bool stub_scan(void* /*user*/) noexcept
{
    for (u32 i = 0; i < kApCount; ++i)
    {
        notyvos_wifi_add_scan_result(g_aps[i].ssid, g_aps[i].bssid,
                                     g_aps[i].channel, g_aps[i].rssi,
                                     g_aps[i].security);
    }
    return true;
}

bool stub_join(void* /*user*/, const char* ssid, const u8* /*bssid*/) noexcept
{
    if (!ssid || !ssid[0])
        return false;
    // Record the requested SSID as current. The WPA handshake is driven
    // by wpa::connect() in parallel; this function just marks
    // association as accepted.
    u32 n = 0;
    while (ssid[n] && n < wifi::kMaxSsid)
    {
        g_adapter.current_ssid[n] = ssid[n];
        ++n;
    }
    g_adapter.current_ssid[n] = 0;
    g_adapter.connected = true;
    log::write(log::Level::Info, "wifi-stub", "associated with '%s'", ssid);
    return true;
}

void stub_disconnect(void* /*user*/) noexcept
{
    g_adapter.connected = false;
    g_adapter.current_ssid[0] = 0;
}

bool stub_send(void* /*user*/, const u8* /*frame*/, usize /*len*/) noexcept
{
    // No real radio; drop silently.
    return true;
}

} // namespace

void init() noexcept
{
    libk::memset(&g_adapter, 0, sizeof(g_adapter));

    g_adapter.name[0] = 'w';
    g_adapter.name[1] = 'l';
    g_adapter.name[2] = 'a';
    g_adapter.name[3] = 'n';
    g_adapter.name[4] = '0';
    g_adapter.name[5] = 0;

    // Pseudo-random MAC. Stable across boots so the UI is deterministic.
    g_adapter.mac[0] = 0x02;
    g_adapter.mac[1] = 0x4E;
    g_adapter.mac[2] = 0x59;
    g_adapter.mac[3] = 0x56;
    g_adapter.mac[4] = 0x4F;
    g_adapter.mac[5] = 0x53;

    g_adapter.channel = 6;
    g_adapter.security = wifi::Security::WPA2_PSK;
    g_adapter.connected = false;
    g_adapter.current_ssid[0] = 0;

    g_adapter.scan = &stub_scan;
    g_adapter.join = &stub_join;
    g_adapter.disconnect = &stub_disconnect;
    g_adapter.send = &stub_send;
    g_adapter.user = nullptr;

    (void)wifi::register_adapter(&g_adapter);

    log::write(log::Level::Info, "wifi-stub",
               "virtual adapter wlan0 registered (%u virtual APs)", kApCount);
}

} // namespace notyvos::net::wifi_stub