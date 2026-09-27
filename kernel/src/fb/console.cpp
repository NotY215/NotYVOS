#include "font8x8.hpp"
#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/fb/framebuffer.hpp>

namespace notyvos::fb
{

namespace
{
constexpr u32 kScale = 2;
constexpr u32 kGlyphW = 8;
constexpr u32 kGlyphH = 8;
constexpr u32 kCellW = kGlyphW * kScale;
constexpr u32 kCellH = kGlyphH * kScale;
constexpr u32 kTabWidth = 4;
constexpr u32 kCursorH = 4;              // underline thickness, pixels
constexpr u32 kCursorColor = 0x00FFFFFF; // bright white

struct State
{
    bool ready = false;
    u32 cols = 0, rows = 0, col = 0, row = 0;
    u32 fg = 0x00E0E0E0;
    u32 bg = 0x00101018;
    bool cursor_drawn = false;
};

State g;
} // namespace

void Console::init() noexcept
{
    if (!Framebuffer::ready())
        return;
    g.cols = Framebuffer::width() / kCellW;
    g.rows = Framebuffer::height() / kCellH;
    g.col = 0;
    g.row = 0;
    g.ready = true;
    Framebuffer::clear(g.bg);
    draw_cursor();
}

bool Console::ready() noexcept
{
    return g.ready;
}

void Console::set_colors(u32 fg_argb, u32 bg_argb) noexcept
{
    erase_cursor();
    g.fg = fg_argb;
    g.bg = bg_argb;
    draw_cursor();
}

u32 Console::fg() noexcept
{
    return g.fg;
}
u32 Console::bg() noexcept
{
    return g.bg;
}
u32 Console::cols() noexcept
{
    return g.cols;
}
u32 Console::rows() noexcept
{
    return g.rows;
}
u32 Console::cursor_col() noexcept
{
    return g.col;
}
u32 Console::cursor_row() noexcept
{
    return g.row;
}

void Console::clear() noexcept
{
    if (!g.ready)
        return;
    erase_cursor();
    Framebuffer::clear(g.bg);
    g.col = 0;
    g.row = 0;
    g.cursor_drawn = false;
    draw_cursor();
}

void Console::set_cursor(u32 col, u32 row) noexcept
{
    if (!g.ready)
        return;
    erase_cursor();
    g.col = (col < g.cols) ? col : (g.cols - 1);
    g.row = (row < g.rows) ? row : (g.rows - 1);
    draw_cursor();
}

void Console::refresh_cursor() noexcept
{
    if (!g.ready)
        return;
    erase_cursor();
    draw_cursor();
}

void Console::draw_glyph(u32 col, u32 row, char c) noexcept
{
    if (!g.ready)
        return;
    if (c < 0x20 || c > 0x7E)
        c = '?';
    const u8* rows = notyvos::fb::kFont8x8[static_cast<int>(c) - 0x20];
    Framebuffer::blit_glyph_8x8(col * kCellW, row * kCellH, kScale, rows, g.fg, g.bg);
}

void Console::erase_cursor() noexcept
{
    if (!g.ready || !g.cursor_drawn)
        return;
    const u32 x = g.col * kCellW;
    const u32 y = g.row * kCellH + kCellH - kCursorH;
    Framebuffer::fill_rect(x, y, kCellW, kCursorH, g.bg);
    g.cursor_drawn = false;
}

void Console::draw_cursor() noexcept
{
    if (!g.ready)
        return;
    const u32 x = g.col * kCellW;
    const u32 y = g.row * kCellH + kCellH - kCursorH;
    Framebuffer::fill_rect(x, y, kCellW, kCursorH, kCursorColor);
    g.cursor_drawn = true;
}

void Console::scroll_up_one() noexcept
{
    if (!g.ready)
        return;
    Framebuffer::copy_strip(kCellH, 0, Framebuffer::height() - kCellH);
    Framebuffer::fill_rect(0, Framebuffer::height() - kCellH, Framebuffer::width(), kCellH, g.bg);
    g.cursor_drawn = false;
}

void Console::newline() noexcept
{
    g.col = 0;
    if (++g.row >= g.rows)
    {
        scroll_up_one();
        g.row = g.rows - 1;
    }
}

void Console::backspace() noexcept
{
    if (g.col == 0)
        return;
    --g.col;
    draw_glyph(g.col, g.row, ' ');
}

void Console::advance_cursor() noexcept
{
    if (++g.col >= g.cols)
        newline();
}

void Console::put(char c) noexcept
{
    if (!g.ready)
        return;

    erase_cursor();

    if (c == '\n')
    {
        newline();
        draw_cursor();
        return;
    }
    if (c == '\r')
    {
        g.col = 0;
        draw_cursor();
        return;
    }
    if (c == '\t')
    {
        for (u32 i = 0; i < kTabWidth; ++i)
        {
            draw_glyph(g.col, g.row, ' ');
            advance_cursor();
        }
        draw_cursor();
        return;
    }
    if (c == '\b')
    {
        backspace();
        draw_cursor();
        return;
    }
    if (c == '\0')
    {
        draw_cursor();
        return;
    }

    draw_glyph(g.col, g.row, c);
    advance_cursor();
    draw_cursor();
}

void Console::puts(const char* s) noexcept
{
    if (!s)
        return;
    while (*s)
        put(*s++);
}

} // namespace notyvos::fb
