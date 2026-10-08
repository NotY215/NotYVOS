#pragma once
#include <kernel/net/net.hpp>
#include <kernel/net/wifi.hpp>

namespace notyvos::net::wpa
{

constexpr u32 kPmkLen   = 32;
constexpr u32 kPtkLen   = 64;
constexpr u32 kNonceLen = 32;
constexpr u32 kMicLen   = 16;
constexpr u32 kKckLen   = 16;
constexpr u32 kKekLen   = 16;
constexpr u32 kTkLen    = 16;

enum class State : u8
{
    Idle = 0,
    Associating,
    EapolStart,
    WaitMsg1,
    WaitMsg3,
    WaitGroupMsg,
    Connected,
    Failed,
};

struct Session
{
    State state;
    char  adapter[kIfNameMax];
    char  ssid[wifi::kMaxSsid + 1];
    u8    bssid[6];

    u8    psk[32];        // Pre-shared key (from PBKDF2 or cached)
    u8    anonce[kNonceLen];
    u8    snonce[kNonceLen];
    u8    ptk[kPtkLen];   // KCK || KEK || TK
    u16   eapol_replay;

    bool  have_psk;
    bool  have_anonce;
    bool  have_snonce;

    u64   last_tick;
    u32   retries;
};

void init() noexcept;

// Begin the WPA handshake. `passphrase` is the ASCII WPA-PSK.
// Internally PBKDF2(passphrase, ssid, 4096, 32).
bool connect(const char* adapter_name, const char* ssid, const char* passphrase) noexcept;

void disconnect(const char* adapter_name) noexcept;

// Called from the adapter's RX path when a management or EAPOL frame
// arrives. The supplicant parses EAPOL-Key frames.
void handle_eapol(const char* adapter_name, const u8* frame, usize len) noexcept;

// Called by the compositor or scheduler tick so retry timers advance.
void tick() noexcept;

State state(const char* adapter_name) noexcept;

// Diagnostics.
u64 handshakes_completed() noexcept;
u64 handshakes_failed() noexcept;

// Helpers exposed for drivers and for the management UI.
void derive_psk(const char* passphrase, const char* ssid, u8 out_psk[32]) noexcept;

} // namespace notyvos::net::wpa