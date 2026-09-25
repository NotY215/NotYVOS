#include <kernel/log.hpp>
#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/fb/framebuffer.hpp>

namespace notyvos::log {

namespace {
Level       g_level = Level::Info;
u32         g_row   = 0;
constexpr u32 kFg = 0x00E0E0E0;
constexpr u32 kBg = 0x00101018;

const char* level_str(Level l) noexcept {
    switch (l) {
        case Level::Debug: return "DBG";
        case Level::Info:  return "INF";
        case Level::Warn:  return "WRN";
        case Level::Error: return "ERR";
    }
    return "???";
}

u32 level_color(Level l) noexcept {
    switch (l) {
        case Level::Debug: return 0x0080A0A0;
        case Level::Info:  return 0x00E0E0E0;
        case Level::Warn:  return 0x00E0C060;
        case Level::Error: return 0x00E06060;
    }
    return kFg;
}

void putc(char c) {
    arch::x86_64::SerialPort::write_char(c);
    if (!fb::Framebuffer::ready()) return;
    if (c == '\n') {
        g_row++;
        return;
    }
    // Log lines start at column 0 for simplicity.
    // Column position is tracked by the caller via static column.
}
} // namespace

void init() noexcept {
    g_row = 0;
    if (fb::Framebuffer::ready()) fb::Framebuffer::clear(kBg);
}

void set_level(Level lvl) noexcept { g_level = lvl; }
Level level() noexcept { return g_level; }

void write(Level lvl, const char* tag, const char* fmt, ...) noexcept {
    if (static_cast<int>(lvl) < static_cast<int>(g_level)) return;

    // ---- serial ----
    arch::x86_64::SerialPort::write("[");
    arch::x86_64::SerialPort::write(level_str(lvl));
    arch::x86_64::SerialPort::write("] ");
    if (tag) { arch::x86_64::SerialPort::write(tag); arch::x86_64::SerialPort::write(": "); }

    // ---- framebuffer (prefix) ----
    u32 col = 0;
    if (fb::Framebuffer::ready()) {
        fb::Framebuffer::draw_text(col, g_row, "[", kFg, kBg); col += 1;
        fb::Framebuffer::draw_text(col, g_row, level_str(lvl), level_color(lvl), kBg); col += 3;
        fb::Framebuffer::draw_text(col, g_row, "] ", kFg, kBg); col += 2;
        if (tag) {
            fb::Framebuffer::draw_text(col, g_row, tag, 0x0080C0FF, kBg); col += static_cast<u32>(4); // "tag\0" approx
            fb::Framebuffer::draw_text(col, g_row, ": ", kFg, kBg); col += 2;
        }
    }

    // ---- format: only %s, %c, %d, %u, %x, %p for Phase 0 ----
    // We deliberately keep this tiny; a full printf arrives in Phase 1.
    // Simplify by emitting characters and formatting numbers inline.
    // (In Phase 0 the caller mostly uses literal strings; the varargs are
    //  consumed by a small dispatcher below.)
    // For brevity in Phase 0 we implement %s, %c, %u, %d, %x only.
    // NOTE: This is deliberately not a general printf yet.

    // We implement a minimal walker:
    char tmp[32];
    auto to_serial = [](char c) { arch::x86_64::SerialPort::write_char(c); };
    auto to_fb     = [&](char c) {
        if (!fb::Framebuffer::ready()) return;
        if (c == '\n') { g_row++; col = 0; return; }
        char s[2] = { c, 0 };
        fb::Framebuffer::draw_text(col, g_row, s, kFg, kBg);
        col += 1;
    };

    va_list ap;
    va_start(ap, fmt);
    const char* p = fmt;
    while (p && *p) {
        if (*p != '%') { to_serial(*p); to_fb(*p); ++p; continue; }
        ++p;
        switch (*p) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) { to_serial(*s); to_fb(*s); ++s; }
                break;
            }
            case 'c': {
                char c = static_cast<char>(va_arg(ap, int));
                to_serial(c); to_fb(c);
                break;
            }
            case 'u': {
                u64 v = va_arg(ap, u64);
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v) { tmp[n++] = static_cast<char>('0' + (v % 10)); v /= 10; }
                while (n--) { to_serial(tmp[n]); to_fb(tmp[n]); }
                break;
            }
            case 'd': {
                i64 v = va_arg(ap, i64);
                if (v < 0) { to_serial('-'); to_fb('-'); v = -v; }
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v) { tmp[n++] = static_cast<char>('0' + (v % 10)); v /= 10; }
                while (n--) { to_serial(tmp[n]); to_fb(tmp[n]); }
                break;
            }
            case 'x': {
                u64 v = va_arg(ap, u64);
                const char* hex = "0123456789abcdef";
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v) { tmp[n++] = hex[v & 0xF]; v >>= 4; }
                while (n--) { to_serial(tmp[n]); to_fb(tmp[n]); }
                break;
            }
            case '%': to_serial('%'); to_fb('%'); break;
            default:  to_serial('%'); to_fb('%'); to_serial(*p); to_fb(*p); break;
        }
        ++p;
    }
    va_end(ap);

    arch::x86_64::SerialPort::write_char('\n');
    if (fb::Framebuffer::ready()) g_row++;
}
} // namespace notyvos::log