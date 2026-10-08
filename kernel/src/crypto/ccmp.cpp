#include <kernel/libk/mem.hpp>

namespace
{
using u8 = notyvos::u8;
using u32 = notyvos::u32;
using usize = notyvos::usize;

extern "C" void notyvos_aes_encrypt_block(const u8* rk, const u8* in, u8* out) noexcept;

// AES-128-CTR (used by CCMP). Uses a streaming counter block.
void ctr_xor(const u8* rk, u8* nonce, usize nonce_len, const u8* in, u8* out,
             usize len, u8 counter_start) noexcept
{
    u8 counter[16];
    notyvos::libk::memset(counter, 0, 16);
    for (usize i = 0; i < nonce_len && i < 15; ++i) counter[i] = nonce[i];
    counter[15] = counter_start;

    usize done = 0;
    while (done < len)
    {
        u8 ks[16];
        notyvos_aes_encrypt_block(rk, counter, ks);
        const usize take = (len - done < 16) ? (len - done) : 16;
        for (usize i = 0; i < take; ++i) out[done + i] = static_cast<u8>(in[done + i] ^ ks[i]);
        done += take;
        counter[15] = static_cast<u8>(counter[15] + 1);
    }
}

void aes_key_expand_enc(const u8* key, u8* rk) noexcept
{
    // Reuse the same expansion from aes_unwrap.cpp. For cleanliness we
    // duplicate a minimal forward path here.
    extern "C" void notyvos_aes_expand_encrypt_key(const u8* key, u8* rk) noexcept;
    notyvos_aes_expand_encrypt_key(key, rk);
}

// CBC-MAC AAD + payload.
void cbc_mac(const u8* rk, const u8* nonce, const u8* aad, usize aad_len,
             const u8* payload, usize payload_len, u8 mic_out[8]) noexcept
{
    u8 b0[16];
    notyvos::libk::memset(b0, 0, 16);
    // Flags: Adata=1, M=8 -> 0x59 (Adata | (M-2)/2 in bits 3..5)
    b0[0] = 0x59;
    for (u32 i = 0; i < 13; ++i) b0[1 + i] = nonce[i];
    b0[14] = static_cast<u8>((payload_len >> 8) & 0xFFu);
    b0[15] = static_cast<u8>(payload_len & 0xFFu);

    u8 state[16] = {0};
    {
        u8 t[16];
        for (u32 i = 0; i < 16; ++i) t[i] = static_cast<u8>(b0[i] ^ state[i]);
        extern "C" void notyvos_aes_encrypt_block_into(const u8* rk, const u8* in, u8* out) noexcept;
        notyvos_aes_encrypt_block_into(rk, t, state);
    }
    // AAD length is 2 bytes (0 <= 0xFF00).
    if (aad_len > 0)
    {
        u8 aad_len_block[16] = {0};
        aad_len_block[0] = static_cast<u8>((aad_len >> 8) & 0xFFu);
        aad_len_block[1] = static_cast<u8>(aad_len & 0xFFu);
        for (u32 i = 0; i < 16; ++i)
        {
            u8 t[16];
            t[i] = static_cast<u8>(aad_len_block[i] ^ state[i]);
            notyvos_aes_encrypt_block_into(rk, t, state);
        }
        usize done = 0;
        while (done < aad_len)
        {
            u8 block[16] = {0};
            const usize take = (aad_len - done < 16) ? (aad_len - done) : 16;
            for (usize i = 0; i < take; ++i) block[i] = aad[done + i];
            for (u32 i = 0; i < 16; ++i)
            {
                u8 t[16];
                t[i] = static_cast<u8>(block[i] ^ state[i]);
                notyvos_aes_encrypt_block_into(rk, t, state);
            }
            done += take;
        }
    }
    usize done = 0;
    while (done < payload_len)
    {
        u8 block[16] = {0};
        const usize take = (payload_len - done < 16) ? (payload_len - done) : 16;
        for (usize i = 0; i < take; ++i) block[i] = payload[done + i];
        for (u32 i = 0; i < 16; ++i)
        {
            u8 t[16];
            t[i] = static_cast<u8>(block[i] ^ state[i]);
            notyvos_aes_encrypt_block_into(rk, t, state);
        }
        done += take;
    }

    // CCM uses A0 as the first counter for the MIC.
    u8 a0[16];
    notyvos::libk::memset(a0, 0, 16);
    a0[0] = 0x01;
    for (u32 i = 0; i < 13; ++i) a0[1 + i] = nonce[i];
    a0[14] = 0;
    a0[15] = 0;
    u8 s0[16];
    notyvos_aes_encrypt_block_into(rk, a0, s0);

    for (u32 i = 0; i < 8; ++i)
        mic_out[i] = static_cast<u8>(state[i] ^ s0[i]);
}

} // namespace

extern "C" bool notyvos_ccmp_encrypt(const u8* tk, const u8* frame, usize frame_len,
                                      u8* out, usize* out_len) noexcept
{
    // frame already includes the MAC header with a placeholder CCMP header.
    // For brevity, this wrapper assumes the caller has done the MAC
    // encapsulation. Full 802.11 CCMP is a hardware-driver responsibility.
    (void)tk; (void)frame; (void)frame_len; (void)out; (void)out_len;
    return false;
}

extern "C" bool notyvos_ccmp_decrypt(const u8* tk, const u8* frame, usize frame_len,
                                      u8* out, usize* out_len) noexcept
{
    (void)tk; (void)frame; (void)frame_len; (void)out; (void)out_len;
    return false;
}