#include <kernel/libk/mem.hpp>

namespace notyvos::libk
{

void* memcpy(void* dst, const void* src, usize n) noexcept
{
    auto* d = static_cast<u8*>(dst);
    const auto* s = static_cast<const u8*>(src);
    for (usize i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}

void* memmove(void* dst, const void* src, usize n) noexcept
{
    auto* d = static_cast<u8*>(dst);
    const auto* s = static_cast<const u8*>(src);
    if (d == s || n == 0)
        return dst;
    if (d < s)
    {
        for (usize i = 0; i < n; ++i)
            d[i] = s[i];
    }
    else
    {
        for (usize i = n; i > 0; --i)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

void* memset(void* dst, int c, usize n) noexcept
{
    auto* d = static_cast<u8*>(dst);
    const u8 v = static_cast<u8>(c);
    for (usize i = 0; i < n; ++i)
        d[i] = v;
    return dst;
}

int memcmp(const void* a, const void* b, usize n) noexcept
{
    const auto* x = static_cast<const u8*>(a);
    const auto* y = static_cast<const u8*>(b);
    for (usize i = 0; i < n; ++i)
    {
        if (x[i] != y[i])
            return (x[i] < y[i]) ? -1 : 1;
    }
    return 0;
}

} // namespace notyvos::libk
