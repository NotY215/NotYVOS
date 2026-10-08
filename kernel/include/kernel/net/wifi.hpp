#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::wifi
{

constexpr u32 kMaxSsid = 32;
constexpr u32 kMaxScanResults = 32;

enum class Security : u8
{
    Open     = 0,
    WEP      = 1,
    WPA      = 2,
    WPA2_PSK = 3,
    WPA2_ENT = 4,
    WPA3     = 5,
};

struct ScanResult
{
    char ssid[kMaxSsid + 1];
    u8   bssid[6];
    u8   channel;
    i8   signal_dbm;
    Security security;
};

// Driver-side callbacks. Each adapter registers these.
using ScanFn = bool (*)(void* user) noexcept;
using JoinFn = bool (*)(void* user, const char* ssid, const u8* bssid) noexcept;
using DisconnectFn = void (*)(void* user) noexcept;
using SendFn = bool (*)(void* user, const u8* frame, usize len) noexcept;
using RxFrameCb = void (*)(Interface* iface, const u8* frame, usize len) noexcept;

struct Adapter
{
    char name[kIfNameMax];
    u8   mac[6];
    u8   channel;
    Security security;
    bool connected;
    char current_ssid[kMaxSsid + 1];

    // Driver entry points.
    ScanFn scan;
    JoinFn join;
    DisconnectFn disconnect;
    SendFn send;

    void* user;
};

void init() noexcept;
bool register_adapter(Adapter* a) noexcept;
u32 adapter_count() noexcept;
Adapter* adapter_by_index(u32 i) noexcept;
Adapter* adapter_by_name(const char* name) noexcept;

// Trigger a scan on the named adapter. Results are cached.
bool scan(const char* adapter_name) noexcept;
u32  scan_results(const char* adapter_name, ScanResult* out, u32 max) noexcept;

// Ask the named adapter to associate with `ssid`. Does not perform the
// WPA handshake; that is wpa::connect()'s job.
bool associate(const char* adapter_name, const char* ssid) noexcept;

void disconnect(const char* adapter_name) noexcept;

// Called by drivers when a management or data frame arrives.
void deliver_rx(Adapter* a, const u8* frame, usize len) noexcept;

// Called by drivers when association succeeds/fails.
void notify_associated(Adapter* a, bool ok) noexcept;

} // namespace notyvos::net::wifi