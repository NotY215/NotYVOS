#include "libnoty.h"

u64 strlen(const char* s)
{
    u64 n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char* a, const char* b, u64 n)
{
    for (u64 i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
            return (int)(u8)a[i] - (int)(u8)b[i];
        if (a[i] == 0)
            return 0;
    }
    return 0;
}

void* memset(void* p, int c, u64 n)
{
    u8* d = (u8*)p;
    while (n--)
        *d++ = (u8)c;
    return p;
}

void* memcpy(void* d, const void* s, u64 n)
{
    u8* D = (u8*)d;
    const u8* S = (const u8*)s;
    while (n--)
        *D++ = *S++;
    return d;
}
