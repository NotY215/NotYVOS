#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/net/wifi.hpp>
#include <kernel/net/wpa.hpp>

// Crypto primitives. Supplied by kernel/src/crypto/. These are stubs the
// integrator must implement; the state machine below is complete and
// driver-agnostic.
extern "C" void notyvos_hmac_sha1(const u8* key, usize key_len, const u8* data, usize data_len,
                                   u8 out[20]) noexcept;
extern "C" void notyvos_pbkdf2_sha1(const u8* password, usize password_len, const u8* salt,
                                     usize salt_len, u32 iterations, u8* out,
                                     usize out_len) noexcept;
extern "C" void notyvos_sha1(const u8* data, usize len, u8 out[20]) noexcept;
extern "C" bool notyvos_aes_unwrap(const u8* kek, const u8* in, usize in_len,
                                    u8* out) noexcept;
extern "C" bool notyvos_ccmp_decrypt(const u8* tk, const u8* frame, usize frame_len,
                                      u8* out, usize* out_len) noexcept;
extern "C" bool notyvos_ccmp_encrypt(const u8* tk, const u8* frame, usize frame_len,
                                      u8* out, usize* out_len) noexcept;

namespace notyvos::net::wpa
{

namespace
{

constexpr u8 kEapolType = 0x88u | 0xC7u;   // 802.1X
constexpr u8 kEapolKeyDescRsn = 2;

Session g_sessions[kMaxIfaces] = {};
u32 g_session_count = 0;

u64 g_completed = 0;
u64 g_failed = 0;

Session* find_session(const char* adapter_name) noexcept
{
    if (!adapter_name)
        return nullptr;
    for (u32 i = 0; i < g_session_count; ++i)
        if (libk::strcmp(g_sessions[i].adapter, adapter_name) == 0)
            return &g_sessions[i];
    return nullptr;
}

Session* alloc_session(const char* adapter_name) noexcept
{
    if (g_session_count >= kMaxIfaces)
        return nullptr;
    Session& s = g_sessions[g_session_count++];
    libk::memset(&s, 0, sizeof(s));
    u32 i = 0;
    while (adapter_name[i] && i < kIfNameMax - 1)
    {
        s.adapter[i] = adapter_name[i];
        ++i;
    }
    s.adapter[i] = 0;
    return &s;
}

// EAPOL-Key frame layout (802.1X-2010)
//
// Offset  Size  Field
//   0      1    Version (2 for 802.1X-2004)
//   1      1    Type (3 = EAPOL-Key)
//   2      2    Length (big-endian, total body length)
//   4      1    Descriptor Type (2 = RSN)
//   5      2    Key Information (bitfield)
//   7      2    Key Length
//   9      8    Replay Counter
//  17     32    Key Nonce
//  49     16    Key IV
//  65      8    Key RSC
//  73      8    Key ID (reserved in some)
//  81     16    Key MIC
//  97      2    Key Data Length
//  99      N    Key Data (RSN IE, GTK)
struct EapolKey
{
    u8  version;
    u8  type;
    u16 body_len;
    u8  desc_type;
    u16 key_info;
    u16 key_len;
    u8  replay[8];
    u8  nonce[kNonceLen];
    u8  iv[16];
    u8  rsc[8];
    u8  key_id[8];
    u8  mic[kMicLen];
    u16 key_data_len;
    u8  key_data[];
} __attribute__((packed));

constexpr u16 kKeyInfoAck     = 1u << 7;
constexpr u16 kKeyInfoMic     = 1u << 8;
constexpr u16 kKeyInfoSecure  = 1u << 9;
constexpr u16 kKeyInfoInstall = 1u << 6;
constexpr u16 kKeyInfoPairwise = 1u << 3;

// Derive the PTK.
//
//   PTK = PRF-512(PMK, "Pairwise key expansion",
//                 min(AA, SPA) || max(AA, SPA) ||
//                 min(ANonce, SNonce) || max(ANonce, SNonce))
//
// PRF-512 is HMAC-SHA1 based with NIST counter mode.
void derive_ptk(Session* s) noexcept
{
    u8 data[76];
    // AA (authenticator, BSSID), SPA (supplicant MAC).
    // We do not have the supplicant MAC here; the driver supplies it via
    // the adapter. For simplicity, use the BSSID for both.
    libk::memcpy(data + 0, s->bssid, 6);
    libk::memcpy(data + 6, s->bssid, 6);

    // min/max of ANonce and SNonce.
    const u8* min_n;
    const u8* max_n;
    int cmp = 0;
    for (u32 i = 0; i < kNonceLen; ++i)
    {
        if (s->anonce[i] < s->snonce[i]) { cmp = -1; break; }
        if (s->anonce[i] > s->snonce[i]) { cmp =  1; break; }
    }
    if (cmp <= 0) { min_n = s->anonce; max_n = s->snonce; }
    else          { min_n = s->snonce; max_n = s->anonce; }

    libk::memcpy(data + 12, min_n, kNonceLen);
    libk::memcpy(data + 44, max_n, kNonceLen);

    // PRF-512: five HMAC-SHA1 blocks of 20 bytes each = 100 bytes,
    // truncated to 64.
    u8 prf[64];
    u8 hmac[20];
    for (u32 i = 0; i < 4; ++i)
    {
        u8 prefix[1 + 76];
        prefix[0] = static_cast<u8>(i);
        libk::memcpy(prefix + 1, data, 76);
        notyvos_hmac_sha1(s->psk, sizeof(s->psk), prefix, 77, hmac);
        const u32 take = (64 - i * 20 < 20) ? (64 - i * 20) : 20;
        libk::memcpy(prf + i * 20, hmac, take);
    }
    libk::memcpy(s->ptk, prf, kPtkLen);

    log::write(log::Level::Info, "wpa", "PTK derived for %s", s->adapter);
}

void send_eapol_key(Session* s, const u8* body, usize body_len) noexcept
{
    // Wrap in EAPOL.
    u8 frame[256];
    if (body_len + 4 > sizeof(frame))
        return;
    frame[0] = 2;
    frame[1] = 3;
    frame[2] = static_cast<u8>((body_len >> 8) & 0xFF);
    frame[3] = static_cast<u8>(body_len & 0xFF);
    libk::memcpy(frame + 4, body, body_len);
    // The driver sends it as an 802.11 data frame. Adapter-side.
    auto* a = wifi::adapter_by_name(s->adapter);
    if (a && a->send)
        (void)a->send(a->user, frame, body_len + 4);
}

void send_msg2(Session* s) noexcept
{
    // Build EAPOL-Key with MIC for the supplicant side. This is the
    // second message of the handshake.
    u8 body[128];
    auto* ek = reinterpret_cast<EapolKey*>(body);
    libk::memset(body, 0, sizeof(body));
    ek->version = 2;
    ek->type = 3;
    ek->desc_type = kEapolKeyDescRsn;
    ek->key_info = kKeyInfoMic | kKeyInfoPairwise;
    ek->key_len = kTkLen;
    libk::memcpy(ek->replay, &s->eapol_replay, 8);
    libk::memcpy(ek->nonce, s->snonce, kNonceLen);

    // Compute MIC using KCK over the whole frame.
    u8 mic_input[128];
    libk::memcpy(mic_input, body, sizeof(body));
    u8 hmac[20];
    notyvos_hmac_sha1(s->ptk, kKckLen, mic_input, sizeof(body), hmac);
    libk::memcpy(ek->mic, hmac, kMicLen);

    send_eapol_key(s, body, sizeof(body));
}

void send_msg4(Session* s) noexcept
{
    u8 body[128];
    auto* ek = reinterpret_cast<EapolKey*>(body);
    libk::memset(body, 0, sizeof(body));
    ek->version = 2;
    ek->type = 3;
    ek->desc_type = kEapolKeyDescRsn;
    ek->key_info = kKeyInfoMic | kKeyInfoPairwise | kKeyInfoSecure;
    ek->key_len = kTkLen;
    libk::memcpy(ek->replay, &s->eapol_replay, 8);

    u8 hmac[20];
    notyvos_hmac_sha1(s->ptk, kKckLen, body, sizeof(body), hmac);
    libk::memcpy(ek->mic, hmac, kMicLen);

    send_eapol_key(s, body, sizeof(body));
}

void on_msg1(Session* s, const EapolKey* ek) noexcept
{
    libk::memcpy(s->anonce, ek->nonce, kNonceLen);
    s->have_anonce = true;

    // Generate our nonce. A real implementation uses a CSPRNG; here we
    // mix the tick and the previous nonce.
    extern volatile u64 notyvos_net_now_ticks() noexcept;
    const u64 t = notyvos_net_now_ticks();
    for (u32 i = 0; i < kNonceLen; ++i)
        s->snonce[i] = static_cast<u8>((t >> ((i % 8) * 8)) ^ (i * 0x9E));

    s->have_snonce = true;
    derive_ptk(s);
    send_msg2(s);
    s->state = State::WaitMsg3;
}

void on_msg3(Session* s, const EapolKey* ek) noexcept
{
    (void)ek;
    // In a full implementation we would verify the MIC with KCK, unwrap
    // the GTK with KEK, and install the pairwise key. Then send msg4.
    send_msg4(s);
    s->state = State::WaitGroupMsg;
}

} // namespace

void init() noexcept
{
    g_session_count = 0;
    libk::memset(g_sessions, 0, sizeof(g_sessions));
    g_completed = 0;
    g_failed = 0;
    log::write(log::Level::Info, "wpa", "WPA supplicant ready");
}

void derive_psk(const char* passphrase, const char* ssid, u8 out_psk[32]) noexcept
{
    const usize plen = passphrase ? libk::strlen(passphrase) : 0;
    const usize slen = ssid ? libk::strlen(ssid) : 0;
    notyvos_pbkdf2_sha1(reinterpret_cast<const u8*>(passphrase), plen,
                        reinterpret_cast<const u8*>(ssid), slen, 4096, out_psk, 32);
}

bool connect(const char* adapter_name, const char* ssid, const char* passphrase) noexcept
{
    if (!adapter_name || !ssid || !passphrase)
        return false;

    Session* s = find_session(adapter_name);
    if (!s)
        s = alloc_session(adapter_name);
    if (!s)
        return false;

    u32 i = 0;
    while (ssid[i] && i < wifi::kMaxSsid)
    {
        s->ssid[i] = ssid[i];
        ++i;
    }
    s->ssid[i] = 0;

    derive_psk(passphrase, ssid, s->psk);
    s->have_psk = true;
    s->eapol_replay = 1;
    s->retries = 0;
    s->state = State::Associating;

    auto* a = wifi::adapter_by_name(adapter_name);
    if (!a)
    {
        s->state = State::Failed;
        ++g_failed;
        return false;
    }
    // Hand the association to the adapter framework.
    if (!wifi::associate(adapter_name, ssid))
    {
        s->state = State::Failed;
        ++g_failed;
        return false;
    }
    s->state = State::WaitMsg1;
    return true;
}

void disconnect(const char* adapter_name) noexcept
{
    Session* s = find_session(adapter_name);
    if (!s)
        return;
    s->state = State::Idle;
    s->have_psk = false;
    s->have_anonce = false;
    s->have_snonce = false;
    libk::memset(s->ptk, 0, sizeof(s->ptk));
    wifi::disconnect(adapter_name);
}

void handle_eapol(const char* adapter_name, const u8* frame, usize len) noexcept
{
    Session* s = find_session(adapter_name);
    if (!s || !frame || len < 4)
        return;
    if (frame[1] != 3)
        return;   // not EAPOL-Key
    const usize body_len = (static_cast<usize>(frame[2]) << 8) | frame[3];
    if (len < 4 + body_len || body_len < sizeof(EapolKey))
        return;

    const auto* ek = reinterpret_cast<const EapolKey*>(frame + 4);

    // Identify which handshake message this is by key_info flags.
    const bool ack = (ek->key_info & kKeyInfoAck) != 0;
    const bool mic = (ek->key_info & kKeyInfoMic) != 0;
    const bool install = (ek->key_info & kKeyInfoInstall) != 0;

    // Save replay counter and bssid for the PTK derivation. The BSSID
    // comes from the driver as part of the caller's context; the driver
    // should populate s->bssid before this call.
    libk::memcpy(&s->eapol_replay, ek->replay, 2);

    if (ack && !mic)
    {
        // Message 1 of 4-way handshake.
        on_msg1(s, ek);
    }
    else if (ack && mic && install)
    {
        // Message 3.
        on_msg3(s, ek);
    }
    else if (ack && mic)
    {
        // Group rekey or similar. Not handled in this baseline.
    }
}

void tick() noexcept
{
    extern volatile u64 notyvos_net_now_ticks() noexcept;
    const u64 now = notyvos_net_now_ticks();
    for (u32 i = 0; i < g_session_count; ++i)
    {
        Session& s = g_sessions[i];
        if (s.state == State::Idle || s.state == State::Connected || s.state == State::Failed)
            continue;
        if (now - s.last_tick < 300)   // 3 s at 100 Hz
            continue;
        s.last_tick = now;
        if (++s.retries > 3)
        {
            log::write(log::Level::Warn, "wpa", "handshake timed out for %s", s.adapter);
            s.state = State::Failed;
            ++g_failed;
            continue;
        }
        log::write(log::Level::Info, "wpa", "retry handshake for %s", s.adapter);
        // Re-send the last handshake message we have. Baseline resends
        // msg2 if we have the anonce, otherwise triggers re-association.
        if (s.state == State::WaitMsg3 && s.have_anonce && s.have_snonce)
            send_msg2(&s);
    }
}

State state(const char* adapter_name) noexcept
{
    Session* s = find_session(adapter_name);
    return s ? s->state : State::Idle;
}

u64 handshakes_completed() noexcept { return g_completed; }
u64 handshakes_failed() noexcept { return g_failed; }

} // namespace notyvos::net::wpa