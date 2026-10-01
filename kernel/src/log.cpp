#include <stdarg.h>

#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/log.hpp>
#include <kernel/libk/mem.hpp>

namespace notyvos::log
{

namespace
{
Level g_level = Level::Info;

// ---------------------------------------------------------------------------
// Log ring buffer. Every completed line is stored here, independent of the
// console target, so the terminal can be primed with the boot log after
// the compositor comes up.
// ---------------------------------------------------------------------------
constexpr u32 kRingLines = 256;
constexpr u32 kRingCols = 160;
char g_ring[kRingLines][kRingCols];
u32 g_ring_head = 0;  // index of the oldest valid line
u32 g_ring_count = 0; // number of valid lines (<= kRingLines)

void push_ring(const char* line, u32 len) noexcept
{
    const u32 slot = (g_ring_head + g_ring_count) % kRingLines;
    const u32 copy_len = (len < kRingCols - 1) ? len : (kRingCols - 1);
    for (u32 i = 0; i < copy_len; ++i)
        g_ring[slot][i] = line[i];
    g_ring[slot][copy_len] = 0;

    if (g_ring_count < kRingLines)
        ++g_ring_count;
    else
        g_ring_head = (g_ring_head + 1) % kRingLines;
}

const char* level_str(Level l) noexcept
{
    switch (l)
    {
    case Level::Debug:
        return "DBG";
    case Level::Info:
        return "INF";
    case Level::Warn:
        return "WRN";
    case Level::Error:
        return "ERR";
    }
    return "???";
}
u32 level_color(Level l) noexcept
{
    switch (l)
    {
    case Level::Debug:
        return 0x0080A0A0;
    case Level::Info:
        return 0x00E0E0E0;
    case Level::Warn:
        return 0x00E0C060;
    case Level::Error:
        return 0x00E06060;
    }
    return 0x00E0E0E0;
}

enum class Len
{
    None,
    Hh,
    H,
    L,
    Ll,
    Z,
    T
};

struct Spec
{
    bool left = false;
    bool plus = false;
    bool space = false;
    bool hash = false;
    bool zero = false;
    u32 width = 0;
    Len len = Len::None;
    char conv = '\0';
};

template<typename Emit>
void emit_padded(Emit emit, const char* s, usize len, const Spec& sp, bool is_num, char sign_char,
                 const char* prefix, usize prefix_len)
{
    const usize total = len + prefix_len + (sign_char ? 1 : 0);
    const usize pad = (sp.width > total) ? (sp.width - total) : 0;
    const char pad_ch = (sp.zero && is_num && !sp.left) ? '0' : ' ';

    if (!sp.left && pad_ch == ' ')
    {
        for (usize i = 0; i < pad; ++i)
            emit(' ');
    }
    if (sign_char)
        emit(sign_char);
    for (usize i = 0; i < prefix_len; ++i)
        emit(prefix[i]);
    if (!sp.left && pad_ch == '0')
    {
        for (usize i = 0; i < pad; ++i)
            emit('0');
    }
    for (usize i = 0; i < len; ++i)
        emit(s[i]);
    if (sp.left)
    {
        for (usize i = 0; i < pad; ++i)
            emit(' ');
    }
}

template<typename Emit> void emit_uint(Emit emit, u64 v, u32 base, bool upper, const Spec& sp)
{
    char buf[32];
    int n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0)
        buf[n++] = '0';
    while (v)
    {
        buf[n++] = digits[v % base];
        v /= base;
    }

    // Reverse
    for (int i = 0; i < n / 2; ++i)
    {
        char t = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = t;
    }

    char prefix[4];
    usize plen = 0;
    if (sp.hash && base == 16)
    {
        prefix[plen++] = '0';
        prefix[plen++] = upper ? 'X' : 'x';
    }
    else if (sp.hash && base == 8)
    {
        prefix[plen++] = '0';
    }

    emit_padded(emit, buf, static_cast<usize>(n), sp, true, 0, prefix, plen);
}

template<typename Emit> void emit_int(Emit emit, i64 v, const Spec& sp)
{
    char sign = 0;
    if (v < 0)
    {
        sign = '-';
        v = -v;
    }
    else if (sp.plus)
        sign = '+';
    else if (sp.space)
        sign = ' ';

    char buf[32];
    int n = 0;
    u64 uv = static_cast<u64>(v);
    if (uv == 0)
        buf[n++] = '0';
    while (uv)
    {
        buf[n++] = static_cast<char>('0' + (uv % 10));
        uv /= 10;
    }
    for (int i = 0; i < n / 2; ++i)
    {
        char t = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = t;
    }
    emit_padded(emit, buf, static_cast<usize>(n), sp, true, sign, nullptr, 0);
}

template<typename Emit> void emit_str(Emit emit, const char* s, const Spec& sp)
{
    if (!s)
        s = "(null)";
    usize len = 0;
    while (s[len])
        ++len;
    const usize pad = (sp.width > len) ? (sp.width - len) : 0;
    if (!sp.left)
        for (usize i = 0; i < pad; ++i)
            emit(' ');
    for (usize i = 0; i < len; ++i)
        emit(s[i]);
    if (sp.left)
        for (usize i = 0; i < pad; ++i)
            emit(' ');
}

template<typename Emit> void format(Emit emit, const char* fmt, va_list ap)
{
    const char* p = fmt;
    while (p && *p)
    {
        if (*p != '%')
        {
            emit(*p++);
            continue;
        }
        ++p;
        if (*p == '%')
        {
            emit('%');
            ++p;
            continue;
        }

        Spec sp;
        // Flags
        for (;;)
        {
            if (*p == '-')
            {
                sp.left = true;
                ++p;
            }
            else if (*p == '+')
            {
                sp.plus = true;
                ++p;
            }
            else if (*p == ' ')
            {
                sp.space = true;
                ++p;
            }
            else if (*p == '#')
            {
                sp.hash = true;
                ++p;
            }
            else if (*p == '0')
            {
                sp.zero = true;
                ++p;
            }
            else
                break;
        }
        // Width
        while (*p >= '0' && *p <= '9')
        {
            sp.width = sp.width * 10 + static_cast<u32>(*p - '0');
            ++p;
        }
        // Length
        if (*p == 'h' && *(p + 1) == 'h')
        {
            sp.len = Len::Hh;
            p += 2;
        }
        else if (*p == 'h')
        {
            sp.len = Len::H;
            ++p;
        }
        else if (*p == 'l' && *(p + 1) == 'l')
        {
            sp.len = Len::Ll;
            p += 2;
        }
        else if (*p == 'l')
        {
            sp.len = Len::L;
            ++p;
        }
        else if (*p == 'z')
        {
            sp.len = Len::Z;
            ++p;
        }
        else if (*p == 't')
        {
            sp.len = Len::T;
            ++p;
        }

        sp.conv = *p++;
        switch (sp.conv)
        {
        case 'd':
        case 'i':
        {
            i64 v = (sp.len == Len::Ll) ? va_arg(ap, long long) : va_arg(ap, int);
            emit_int(emit, v, sp);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        {
            u64 v = (sp.len == Len::Ll) ? va_arg(ap, unsigned long long)
                                        : static_cast<u64>(va_arg(ap, unsigned int));
            const u32 base = (sp.conv == 'u') ? 10 : (sp.conv == 'o' ? 8 : 16);
            emit_uint(emit, v, base, sp.conv == 'X', sp);
            break;
        }
        case 'p':
        {
            u64 v = reinterpret_cast<u64>(va_arg(ap, void*));
            sp.hash = true;
            emit_uint(emit, v, 16, false, sp);
            break;
        }
        case 's':
        {
            const char* s = va_arg(ap, const char*);
            emit_str(emit, s, sp);
            break;
        }
        case 'c':
        {
            char c = static_cast<char>(va_arg(ap, int));
            emit(c);
            break;
        }
        default:
            emit('%');
            emit(sp.conv);
            break;
        }
    }
}
} // namespace

void init() noexcept
{
    if (fb::Console::ready())
    {
        fb::Console::set_colors(0x00E0E0E0, 0x00101018);
        fb::Console::clear();
    }
}

void set_level(Level lvl) noexcept
{
    g_level = lvl;
}
Level level() noexcept
{
    return g_level;
}

void write(Level lvl, const char* tag, const char* fmt, ...) noexcept
{
    if (static_cast<int>(lvl) < static_cast<int>(g_level))
        return;

    char line_buf[kRingCols];
    u32 line_len = 0;

    const bool console = fb::Console::ready();

    // Single emit path: serial + console + ring capture.
    auto emit = [&](char c)
    {
        arch::x86_64::SerialPort::write_char(c);
        if (console)
            fb::Console::put(c);
        if (line_len < kRingCols - 1)
            line_buf[line_len++] = c;
    };

    if (console)
        fb::Console::set_colors(0x00A0A0A0, 0x00101018);
    emit('[');
    if (console)
        fb::Console::set_colors(level_color(lvl), 0x00101018);
    for (const char* p = level_str(lvl); *p; ++p)
        emit(*p);
    if (console)
        fb::Console::set_colors(0x00A0A0A0, 0x00101018);
    emit(']');
    emit(' ');
    if (tag)
    {
        if (console)
            fb::Console::set_colors(0x0080C0FF, 0x00101018);
        for (const char* p = tag; *p; ++p)
            emit(*p);
        if (console)
            fb::Console::set_colors(0x00A0A0A0, 0x00101018);
        emit(':');
        emit(' ');
    }
    if (console)
        fb::Console::set_colors(0x00E0E0E0, 0x00101018);

    va_list ap;
    va_start(ap, fmt);
    format(emit, fmt, ap);
    va_end(ap);

    emit('\n');
    line_buf[line_len] = 0;

    push_ring(line_buf, line_len);
}
// Ring-buffer replay of log lines. `sink` is called once per stored line,
// oldest first. Used by the compositor to prime the terminal with the
// boot log that occurred before the terminal existed.
using LineSink = void (*)(const char* line, void* user);
void replay(LineSink sink, void* user) noexcept;

// Reset the ring. Only called by tests.
void ring_clear() noexcept;
} // namespace notyvos::log
namespace notyvos::log
{

void replay(LineSink sink, void* user) noexcept
{
    if (!sink)
        return;
    for (u32 i = 0; i < g_ring_count; ++i)
    {
        const u32 slot = (g_ring_head + i) % kRingLines;
        sink(g_ring[slot], user);
    }
}

void ring_clear() noexcept
{
    g_ring_head = 0;
    g_ring_count = 0;
    for (u32 i = 0; i < kRingLines; ++i)
        g_ring[i][0] = 0;
}

} // namespace notyvos::log
