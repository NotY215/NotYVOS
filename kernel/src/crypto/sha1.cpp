#include <kernel/libk/mem.hpp>

extern "C" void notyvos_sha1(const notyvos::u8* data, notyvos::usize len,
                              notyvos::u8 out[20]) noexcept;
extern "C" void notyvos_hmac_sha1(const notyvos::u8* key, notyvos::usize key_len,
                                   const notyvos::u8* data, notyvos::usize data_len,
                                   notyvos::u8 out[20]) noexcept;

namespace
{
using u8 = notyvos::u8;
using u32 = notyvos::u32;
using u64 = notyvos::u64;
using usize = notyvos::usize;

inline u32 rotl32(u32 x, u32 n) noexcept
{
    return (x << n) | (x >> (32 - n));
}

struct Sha1State
{
    u32 h[5];
    u8  buf[64];
    u64 total_bits;
    u32 buf_len;
};

void sha1_init(Sha1State* s) noexcept
{
    s->h[0] = 0x67452301u;
    s->h[1] = 0xEFCDAB89u;
    s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xC3D2E1F0u;
    s->total_bits = 0;
    s->buf_len = 0;
}

void sha1_block(Sha1State* s, const u8* block) noexcept
{
    u32 w[80];
    for (u32 i = 0; i < 16; ++i)
        w[i] = (static_cast<u32>(block[i * 4]) << 24) |
               (static_cast<u32>(block[i * 4 + 1]) << 16) |
               (static_cast<u32>(block[i * 4 + 2]) << 8) |
               static_cast<u32>(block[i * 4 + 3]);
    for (u32 i = 16; i < 80; ++i)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    u32 a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (u32 i = 0; i < 80; ++i)
    {
        u32 f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;             k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;             k = 0xCA62C1D6u; }
        const u32 tmp = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = tmp;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

void sha1_update(Sha1State* s, const u8* data, usize len) noexcept
{
    s->total_bits += static_cast<u64>(len) * 8u;
    usize i = 0;
    while (i < len)
    {
        const usize take = (64 - s->buf_len) < (len - i) ? (64 - s->buf_len) : (len - i);
        for (usize k = 0; k < take; ++k)
            s->buf[s->buf_len + k] = data[i + k];
        s->buf_len += static_cast<u32>(take);
        i += take;
        if (s->buf_len == 64)
        {
            sha1_block(s, s->buf);
            s->buf_len = 0;
        }
    }
}

void sha1_final(Sha1State* s, u8 out[20]) noexcept
{
    const u64 bits = s->total_bits;
    u8 pad = 0x80;
    sha1_update(s, &pad, 1);
    pad = 0x00;
    while (s->buf_len != 56)
        sha1_update(s, &pad, 1);
    u8 lenbuf[8];
    for (u32 i = 0; i < 8; ++i)
        lenbuf[i] = static_cast<u8>((bits >> ((7 - i) * 8)) & 0xFFu);
    sha1_update(s, lenbuf, 8);
    for (u32 i = 0; i < 5; ++i)
    {
        out[i * 4]     = static_cast<u8>((s->h[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = static_cast<u8>((s->h[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = static_cast<u8>((s->h[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = static_cast<u8>(s->h[i] & 0xFFu);
    }
}

} // namespace

extern "C" void notyvos_sha1(const u8* data, usize len, u8 out[20]) noexcept
{
    Sha1State s;
    sha1_init(&s);
    sha1_update(&s, data, len);
    sha1_final(&s, out);
}

extern "C" void notyvos_hmac_sha1(const u8* key, usize key_len, const u8* data,
                                   usize data_len, u8 out[20]) noexcept
{
    u8 k[64];
    notyvos::libk::memset(k, 0, 64);
    if (key_len > 64)
    {
        notyvos_sha1(key, key_len, k);
        for (u32 i = 20; i < 64; ++i) k[i] = 0;
    }
    else
    {
        for (usize i = 0; i < key_len; ++i) k[i] = key[i];
    }
    u8 ipad[64], opad[64];
    for (u32 i = 0; i < 64; ++i)
    {
        ipad[i] = static_cast<u8>(k[i] ^ 0x36u);
        opad[i] = static_cast<u8>(k[i] ^ 0x5Cu);
    }
    Sha1State s;
    sha1_init(&s);
    sha1_update(&s, ipad, 64);
    sha1_update(&s, data, data_len);
    u8 inner[20];
    sha1_final(&s, inner);

    sha1_init(&s);
    sha1_update(&s, opad, 64);
    sha1_update(&s, inner, 20);
    sha1_final(&s, out);
}