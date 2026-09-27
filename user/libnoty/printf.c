#include "libnoty.h"

void putc(char c)
{
    sys_write(1, &c, 1);
}
void puts(const char* s)
{
    sys_write(1, s, strlen(s));
}

void put_uint(u64 v)
{
    char b[24];
    int n = 0;
    if (v == 0)
        b[n++] = '0';
    while (v)
    {
        b[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n--)
        putc(b[n]);
}
void put_int(i64 v)
{
    if (v < 0)
    {
        putc('-');
        v = -v;
    }
    put_uint((u64)v);
}
void put_hex(u64 v)
{
    const char* h = "0123456789abcdef";
    char b[20];
    int n = 0;
    if (v == 0)
        b[n++] = '0';
    while (v)
    {
        b[n++] = h[v & 0xF];
        v >>= 4;
    }
    while (n--)
        putc(b[n]);
}

typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end

void printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (const char* p = fmt; *p; ++p)
    {
        if (*p != '%')
        {
            putc(*p);
            continue;
        }
        ++p;
        switch (*p)
        {
        case 's':
        {
            const char* s = va_arg(ap, const char*);
            puts(s ? s : "(null)");
            break;
        }
        case 'c':
        {
            char c = (char)va_arg(ap, int);
            putc(c);
            break;
        }
        case 'u':
        {
            u64 v = va_arg(ap, u64);
            put_uint(v);
            break;
        }
        case 'd':
        {
            i64 v = va_arg(ap, i64);
            put_int(v);
            break;
        }
        case 'x':
        {
            u64 v = va_arg(ap, u64);
            put_hex(v);
            break;
        }
        case 'p':
        {
            u64 v = (u64)va_arg(ap, void*);
            putc('0');
            putc('x');
            put_hex(v);
            break;
        }
        case '%':
            putc('%');
            break;
        default:
            putc('%');
            putc(*p);
            break;
        }
    }
    va_end(ap);
}
