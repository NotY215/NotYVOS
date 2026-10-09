#include <kernel/libk/mem.hpp>
#include <kernel/types.hpp>

extern "C" void notyvos_aes_expand_encrypt_key(const notyvos::u8* key, notyvos::u8* rk) noexcept;
extern "C" void notyvos_aes_encrypt_block_into(const notyvos::u8* rk, const notyvos::u8* in,
                                               notyvos::u8* out) noexcept;

extern "C" bool notyvos_ccmp_encrypt(const notyvos::u8* tk, const notyvos::u8* frame,
                                     notyvos::usize frame_len, notyvos::u8* out,
                                     notyvos::usize* out_len) noexcept
{
    (void)tk;
    (void)frame;
    (void)frame_len;
    (void)out;
    (void)out_len;
    return false;
}

extern "C" bool notyvos_ccmp_decrypt(const notyvos::u8* tk, const notyvos::u8* frame,
                                     notyvos::usize frame_len, notyvos::u8* out,
                                     notyvos::usize* out_len) noexcept
{
    (void)tk;
    (void)frame;
    (void)frame_len;
    (void)out;
    (void)out_len;
    return false;
}
