#include <kernel/libk/string.hpp>

namespace notyvos::libk
{

usize strlen(const char* s) noexcept
{
    usize n = 0;
    while (s && s[n])
        ++n;
    return n;
}

int strcmp(const char* a, const char* b) noexcept
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return static_cast<int>(static_cast<u8>(*a)) - static_cast<int>(static_cast<u8>(*b));
}

int strncmp(const char* a, const char* b, usize n) noexcept
{
    for (usize i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
        {
            return static_cast<int>(static_cast<u8>(a[i])) -
                   static_cast<int>(static_cast<u8>(b[i]));
        }
        if (a[i] == '\0')
            return 0;
    }
    return 0;
}

char* strcpy(char* dst, const char* src) noexcept
{
    char* d = dst;
    while ((*d++ = *src++))
    {
    }
    return dst;
}

char* strncpy(char* dst, const char* src, usize n) noexcept
{
    usize i = 0;
    for (; i < n && src[i]; ++i)
        dst[i] = src[i];
    for (; i < n; ++i)
        dst[i] = '\0';
    return dst;
}

char* strcat(char* dst, const char* src) noexcept
{
    char* d = dst + strlen(dst);
    while ((*d++ = *src++))
    {
    }
    return dst;
}

} // namespace notyvos::libk

// ---------------------------------------------------------------------------
// Freestanding C-linkage wrappers.
//
// Same reason as in mem.cpp: Clang may emit calls to the bare C symbols
// for optimized string operations.
// ---------------------------------------------------------------------------

extern "C" notyvos::usize strlen(const char* s) noexcept
{
    return notyvos::libk::strlen(s);
}

extern "C" int strcmp(const char* a, const char* b) noexcept
{
    return notyvos::libk::strcmp(a, b);
}

extern "C" int strncmp(const char* a, const char* b, notyvos::usize n) noexcept
{
    return notyvos::libk::strncmp(a, b, n);
}

extern "C" char* strcpy(char* dst, const char* src) noexcept
{
    return notyvos::libk::strcpy(dst, src);
}

extern "C" char* strncpy(char* dst, const char* src, notyvos::usize n) noexcept
{
    return notyvos::libk::strncpy(dst, src, n);
}

extern "C" char* strcat(char* dst, const char* src) noexcept
{
    return notyvos::libk::strcat(dst, src);
}
