#include <kernel/types.hpp>

extern "C" void notyvos_hmac_sha1(const notyvos::u8* key, notyvos::usize key_len,
                                  const notyvos::u8* data, notyvos::usize data_len,
                                  notyvos::u8 out[20]) noexcept;

extern "C" void notyvos_pbkdf2_sha1(const notyvos::u8* password, notyvos::usize password_len,
                                    const notyvos::u8* salt, notyvos::usize salt_len,
                                    notyvos::u32 iterations, notyvos::u8* out,
                                    notyvos::usize out_len) noexcept
{
    using u8 = notyvos::u8;
    using u32 = notyvos::u32;
    using usize = notyvos::usize;

    u32 block_index = 1;
    usize remaining = out_len;

    while (remaining > 0)
    {
        u8 u[20];
        u8 salt_block[256];
        for (usize i = 0; i < salt_len; ++i)
            salt_block[i] = salt[i];
        salt_block[salt_len + 0] = static_cast<u8>((block_index >> 24) & 0xFFu);
        salt_block[salt_len + 1] = static_cast<u8>((block_index >> 16) & 0xFFu);
        salt_block[salt_len + 2] = static_cast<u8>((block_index >> 8) & 0xFFu);
        salt_block[salt_len + 3] = static_cast<u8>(block_index & 0xFFu);

        notyvos_hmac_sha1(password, password_len, salt_block, salt_len + 4, u);
        u8 result[20];
        for (u32 i = 0; i < 20; ++i)
            result[i] = u[i];

        for (u32 iter = 1; iter < iterations; ++iter)
        {
            u8 next[20];
            notyvos_hmac_sha1(password, password_len, u, 20, next);
            for (u32 i = 0; i < 20; ++i)
            {
                u[i] = next[i];
                result[i] = static_cast<u8>(result[i] ^ next[i]);
            }
        }

        const usize take = (remaining < 20) ? remaining : 20;
        for (usize i = 0; i < take; ++i)
            out[(block_index - 1) * 20 + i] = result[i];
        remaining -= take;
        ++block_index;
    }
}
