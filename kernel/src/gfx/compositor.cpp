#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/fb/font8x8.hpp"
#include <kernel/fb/framebuffer.hpp>
#include <kernel/gfx/compositor.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::gfx
{

namespace
{

constexpr u32 kScale = 2;
constexpr u32 kGlyphW = 8;
constexpr u32 kGlyphH = 8;
constexpr u32 kCellW = kGlyphW * kScale;
constexpr u32 kCellH = kGlyphH * kScale;

constexpr u32 kTaskbarH = 30;
constexpr u32 kWinX = 20;
constexpr u32 kWinY = 20;
constexpr u32 kTitleH = 24;

// Colors (0x00RRGGBB).
constexpr u32 kBgTop = 0x00283046;
constexpr u32 kBgBottom = 0x00182032;
constexpr u32 kTaskbar = 0x00181E2A;
constexpr u32 kTaskFg = 0x00C8C8C8;
constexpr u32 kTaskHi = 0x003060A0;
constexpr u32 kTitle = 0x003060A0;
constexpr u32 kTitleFg = 0x00FFFFFF;
constexpr u32 kBorder = 0x00586078;
constexpr u32 kClientBg = 0x00101018;
constexpr u32 kTextFg = 0x00E0E0E0;
constexpr u32 kCursor = 0x00FFFFFF;

bool g_ready = false;
u32 g_term_cols = 0;
u32 g_term_rows = 0;
u32 g_win_w = 0;
u32 g_win_h = 0;

char g_term[80 * 50]; // 4000 cells max
u32 g_term_cursor = 0;
u32 g_clock = 0;

i32 g_cursor_last_x = -1;
i32 g_cursor_last_y = -1;
alignas(8) u32 g_cursor_saved[16 * 16];

// ---- Drawing primitives ----

inline void put_px(u32 x, u32 y, u32 c) noexcept
{
    fb::Framebuffer::put_pixel(x, y, c);
}

void fill(u32 x, u32 y, u32 w, u32 h, u32 c) noexcept
{
    fb::Framebuffer::fill_rect(x, y, w, h, c);
}

void glyph(u32 px, u32 py, char ch, u32 fg, u32 bg) noexcept
{
    if (ch < 0x20 || ch > 0x7E)
        ch = '?';
    const u8* rows = fb::kFont8x8[static_cast<int>(ch) - 0x20];
    fb::Framebuffer::blit_glyph_8x8(px, py, kScale, rows, fg, bg);
}

void text(u32 col, u32 row, const char* s, u32 fg, u32 bg) noexcept
{
    u32 x = col;
    while (*s)
    {
        glyph(x * kCellW, row * kCellH, *s, fg, bg);
        ++x;
        ++s;
    }
}

// ---- Background and taskbar ----

void draw_background() noexcept
{
    const u32 W = fb::Framebuffer::width();
    const u32 H = fb::Framebuffer::height();
    for (u32 y = 0; y < H - kTaskbarH; ++y)
    {
        // Simple horizontal gradient between kBgTop and kBgBottom.
        const u32 t = (y * 32) / (H - kTaskbarH);
        const u32 r = ((kBgTop >> 16) & 0xFF) * (32 - t) / 32 + ((kBgBottom >> 16) & 0xFF) * t / 32;
        const u32 g = ((kBgTop >> 8) & 0xFF) * (32 - t) / 32 + ((kBgBottom >> 8) & 0xFF) * t / 32;
        const u32 b = (kBgTop & 0xFF) * (32 - t) / 32 + (kBgBottom & 0xFF) * t / 32;
        const u32 c = (r << 16) | (g << 8) | b;
        fill(0, y, W, 1, c);
    }
}

void draw_taskbar() noexcept
{
    const u32 W = fb::Framebuffer::width();
    const u32 H = fb::Framebuffer::height();
    fill(0, H - kTaskbarH, W, kTaskbarH, kTaskbar);
    // Left text
    text(1, (H - kTaskbarH) / kCellH + 1, "NOTYVOS", kTaskFg, kTaskbar);
    // Right: clock
    char buf[16];
    int n = 0;
    u32 s = g_clock;
    if (s == 0)
        buf[n++] = '0';
    while (s)
    {
        buf[n++] = static_cast<char>('0' + s % 10);
        s /= 10;
    }
    buf[n++] = 's';
    buf[n++] = ' ';
    // Reverse
    for (int i = 0; i < n / 2; ++i)
    {
        char t = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = t;
    }
    buf[n] = 0;
    // Draw near right edge
    const u32 tw = static_cast<u32>(n) * kCellW;
    const u32 tx = W - tw - 8;
    const u32 ty = H - kTaskbarH + (kTaskbarH - kCellH) / 2;
    for (int i = 0; i < n; ++i)
        glyph(tx + static_cast<u32>(i) * kCellW, ty, buf[i], kTaskFg, kTaskbar);
}

// ---- Window ----

void draw_window_frame() noexcept
{
    const u32 W = fb::Framebuffer::width();
    const u32 H = fb::Framebuffer::height();

    g_win_w = W - 2 * kWinX;
    g_win_h = H - kTaskbarH - kWinY - 20;

    // Border
    fill(kWinX - 1, kWinY - 1, g_win_w + 2, g_win_h + 2, kBorder);
    // Title bar
    fill(kWinX, kWinY, g_win_w, kTitleH, kTitle);
    // Title text
    text(kWinX / kCellW + 1, kWinY / kCellH + 1, "Terminal", kTitleFg, kTitle);
    // Client area
    fill(kWinX, kWinY + kTitleH, g_win_w, g_win_h - kTitleH, kClientBg);
}

void redraw_terminal() noexcept
{
    // Compute grid origin in pixels.
    const u32 gx = kWinX;
    const u32 gy = kWinY + kTitleH;

    // Fill client area with default bg.
    fill(gx, gy, g_win_w, g_win_h - kTitleH, kClientBg);

    // Draw text cells.
    for (u32 row = 0; row < g_term_rows; ++row)
    {
        for (u32 col = 0; col < g_term_cols; ++col)
        {
            const char ch = g_term[row * g_term_cols + col];
            if (ch == 0 || ch == ' ')
                continue;
            glyph(gx + col * kCellW, gy + row * kCellH, ch, kTextFg, kClientBg);
        }
    }
}

// ---- Cursor ----

void save_cursor_bg(i32 x, i32 y) noexcept
{
    for (u32 j = 0; j < 16; ++j)
    {
        for (u32 i = 0; i < 16; ++i)
        {
            const u32 px = static_cast<u32>(x) + i;
            const u32 py = static_cast<u32>(y) + j;
            // We do not have read_pixel exposed; store a solid color.
            // A proper compositor keeps a back buffer. For 3A we accept the
            // cursor smear by drawing a filled rect at the old position.
            (void)px;
            (void)py;
            g_cursor_saved[j * 16 + i] = kClientBg;
        }
    }
}

// Arrow cursor, 12x16 pixel bitmap. Each row is 16 bits, MSB leftmost.
const u16 kArrow[16] = {
    0b1000000000000000, 0b1100000000000000, 0b1110000000000000, 0b1111000000000000,
    0b1111100000000000, 0b1111110000000000, 0b1111111000000000, 0b1111111100000000,
    0b1111111110000000, 0b1111111111000000, 0b1111100000000000, 0b1101100000000000,
    0b0000110000000000, 0b0000110000000000, 0b0000011000000000, 0b0000011000000000,
};

void draw_cursor(i32 x, i32 y) noexcept
{
    for (u32 j = 0; j < 16; ++j)
    {
        const u16 bits = kArrow[j];
        for (u32 i = 0; i < 16; ++i)
        {
            if ((bits << i) & 0x8000)
            {
                put_px(static_cast<u32>(x) + i, static_cast<u32>(y) + j, kCursor);
            }
        }
    }
}

void redraw_cursor_area(i32 x, i32 y) noexcept
{
    // Redraw the region under the previous cursor. Since we do not have a
    // back buffer yet, we redraw the whole desktop region below/around the
    // cursor by refilling from known layout.
    //
    // For 3A this is a "redraw whole desktop" approach for simplicity.
    // Phase 3B adds a proper back buffer.

    // Fallback: fill the cursor bounding box with client bg. This causes
    // visible smears when the cursor is over the taskbar or title bar, but
    // is acceptable for the first desktop iteration.
    fill(static_cast<u32>(x), static_cast<u32>(y), 16, 16, kClientBg);
}

} // namespace

void Compositor::init() noexcept
{
    if (!fb::Framebuffer::ready())
        return;

    const u32 W = fb::Framebuffer::width();
    const u32 H = fb::Framebuffer::height();

    // Compute client area grid.
    g_win_w = W - 2 * kWinX;
    g_win_h = H - kTaskbarH - kWinY - 20;

    const u32 client_w = g_win_w;
    const u32 client_h = g_win_h - kTitleH;
    g_term_cols = client_w / kCellW;
    g_term_rows = client_h / kCellH;
    if (g_term_cols > 80)
        g_term_cols = 80;
    if (g_term_rows > 50)
        g_term_rows = 50;

    for (u32 i = 0; i < g_term_cols * g_term_rows; ++i)
        g_term[i] = 0;
    g_term_cursor = 0;

    draw_background();
    draw_taskbar();
    draw_window_frame();
    redraw_terminal();

    g_ready = true;
    log::write(log::Level::Info, "comp", "desktop %ux%u, terminal %ux%u", static_cast<u64>(W),
               static_cast<u64>(H), static_cast<u64>(g_term_cols), static_cast<u64>(g_term_rows));
}

bool Compositor::ready() noexcept
{
    return g_ready;
}

u32 Compositor::term_cols() noexcept
{
    return g_term_cols;
}
u32 Compositor::term_rows() noexcept
{
    return g_term_rows;
}

void Compositor::term_clear() noexcept
{
    for (u32 i = 0; i < g_term_cols * g_term_rows; ++i)
        g_term[i] = 0;
    g_term_cursor = 0;
    redraw_terminal();
}

void Compositor::term_scroll() noexcept
{
    // Shift every row up by one.
    const u32 row_bytes = g_term_cols;
    libk::memmove(g_term, g_term + row_bytes, (g_term_rows - 1) * row_bytes);
    for (u32 i = 0; i < g_term_cols; ++i)
    {
        g_term[(g_term_rows - 1) * g_term_cols + i] = 0;
    }
    g_term_cursor = (g_term_rows - 1) * g_term_cols;
    redraw_terminal();
}

void Compositor::term_put(char c) noexcept
{
    if (!g_ready)
        return;

    if (c == '\n')
    {
        const u32 col = g_term_cursor % g_term_cols;
        g_term_cursor += g_term_cols - col;
    }
    else if (c == '\r')
    {
        const u32 col = g_term_cursor % g_term_cols;
        g_term_cursor -= col;
    }
    else if (c == '\t')
    {
        for (u32 i = 0; i < 4; ++i)
            term_put(' ');
        return;
    }
    else if (c == '\b')
    {
        if (g_term_cursor > 0)
        {
            --g_term_cursor;
            g_term[g_term_cursor] = ' ';
        }
    }
    else
    {
        g_term[g_term_cursor++] = c;
    }

    if (g_term_cursor >= g_term_cols * g_term_rows)
    {
        term_scroll();
    }

    // Redraw the affected cell.
    const u32 row = g_term_cursor / g_term_cols;
    const u32 col = g_term_cursor % g_term_cols;
    const u32 gx = kWinX;
    const u32 gy = kWinY + kTitleH;

    if (c == '\n' || c == '\r' || c == '\t' || c == '\b')
    {
        // Simple: redraw the whole terminal for these.
        redraw_terminal();
        return;
    }

    glyph(gx + col * kCellW, gy + row * kCellH, g_term[g_term_cursor - 1], kTextFg, kClientBg);
}

void Compositor::update_clock(u64 seconds) noexcept
{
    g_clock = static_cast<u32>(seconds);
    draw_taskbar();
}

void Compositor::tick() noexcept
{
    if (!g_ready)
        return;
    const i32 x = arch::x86_64::mouse_x();
    const i32 y = arch::x86_64::mouse_y();
    if (x == g_cursor_last_x && y == g_cursor_last_y)
        return;

    if (g_cursor_last_x >= 0 && g_cursor_last_y >= 0)
    {
        redraw_cursor_area(g_cursor_last_x, g_cursor_last_y);
    }
    draw_cursor(x, y);
    g_cursor_last_x = x;
    g_cursor_last_y = y;
}

} // namespace notyvos::gfx
