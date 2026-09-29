#include "../fb/font8x8.hpp"
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/gfx/compositor.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::gfx
{

namespace
{

constexpr u32 kScale = 2;
constexpr u32 kCellW = 8 * kScale;
constexpr u32 kCellH = 8 * kScale;

constexpr u32 kTaskbarH = 30;
constexpr u32 kTitleH = 26;
constexpr u32 kBorder = 1;
constexpr u32 kCloseBoxW = 18;

constexpr u32 kBgTop = 0x00283046;
constexpr u32 kBgBottom = 0x00182032;
constexpr u32 kTaskbar = 0x00181E2A;
constexpr u32 kTaskFg = 0x00C8C8C8;
constexpr u32 kTaskBtn = 0x00305070;
constexpr u32 kTaskBtnOn = 0x005080B0;
constexpr u32 kTitle = 0x003060A0;
constexpr u32 kTitleOff = 0x00405060;
constexpr u32 kTitleFg = 0x00FFFFFF;
constexpr u32 kBorderFg = 0x00586078;
constexpr u32 kClientBg = 0x00101018;
constexpr u32 kTextFg = 0x00E0E0E0;
constexpr u32 kCursorFg = 0x00FFFFFF;
constexpr u32 kCursorSh = 0x00000000;

bool g_ready = false;
u32 g_w = 0, g_h = 0;
u32* g_back = nullptr;
u32* g_wallpaper = nullptr; // nullptr = use gradient

Window g_windows[kMaxWindows] = {};
u32 g_win_count = 0;

u32 g_term_cols = 0;
u32 g_term_rows = 0;
char g_term[120 * 60];
u32 g_term_cursor = 0;

u64 g_clock_sec = 0;

i32 g_prev_mx = -1;
i32 g_prev_my = -1;
bool g_prev_left = false;

i32 g_drag_win = -1;
i32 g_drag_off_x = 0;
i32 g_drag_off_y = 0;

// g_dirty_windows is set by IRQ handlers. g_dirty_full forces a full repaint.
volatile bool g_dirty_full = true;

inline void bput(u32 x, u32 y, u32 c) noexcept
{
    if (x >= g_w || y >= g_h || !g_back)
        return;
    g_back[y * g_w + x] = c;
}

void bfill(i32 x, i32 y, i32 w, i32 h, u32 c) noexcept
{
    if (!g_back)
        return;
    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + w;
    if (x1 > static_cast<i32>(g_w))
        x1 = static_cast<i32>(g_w);
    i32 y1 = y + h;
    if (y1 > static_cast<i32>(g_h))
        y1 = static_cast<i32>(g_h);
    if (x0 >= x1 || y0 >= y1)
        return;
    for (i32 j = y0; j < y1; ++j)
    {
        u32* row = g_back + static_cast<u32>(j) * g_w;
        for (i32 i = x0; i < x1; ++i)
            row[i] = c;
    }
}

void bglyph(i32 px, i32 py, char ch, u32 fg, u32 bg) noexcept
{
    if (ch < 0x20 || ch > 0x7E)
        ch = '?';
    const u8* rows = notyvos::fb::kFont8x8[static_cast<int>(ch) - 0x20];
    for (u32 gy = 0; gy < 8; ++gy)
    {
        const u8 bits = rows[gy];
        for (u32 gx = 0; gx < 8; ++gx)
        {
            const bool on = (bits >> (7 - gx)) & 1u;
            const u32 col = on ? fg : bg;
            for (u32 sy = 0; sy < kScale; ++sy)
            {
                for (u32 sx = 0; sx < kScale; ++sx)
                {
                    bput(
                        static_cast<u32>(px + static_cast<i32>(gx * kScale) + static_cast<i32>(sx)),
                        static_cast<u32>(py + static_cast<i32>(gy * kScale) + static_cast<i32>(sy)),
                        col);
                }
            }
        }
    }
}

void btext(i32 px, i32 py, const char* s, u32 fg, u32 bg) noexcept
{
    i32 x = px;
    while (*s)
    {
        bglyph(x, py, *s, fg, bg);
        x += static_cast<i32>(kCellW);
        ++s;
    }
}

void draw_background() noexcept
{
    if (g_wallpaper)
    {
        for (u32 y = 0; y < g_h; ++y)
        {
            const u32* src = g_wallpaper + y * g_w;
            u32* dst = g_back + y * g_w;
            libk::memcpy(dst, src, g_w * 4);
        }
        return;
    }
    for (u32 y = 0; y < g_h - kTaskbarH; ++y)
    {
        const u32 t = (y * 32) / (g_h - kTaskbarH);
        const u32 r = ((kBgTop >> 16) & 0xFF) * (32 - t) / 32 + ((kBgBottom >> 16) & 0xFF) * t / 32;
        const u32 g = ((kBgTop >> 8) & 0xFF) * (32 - t) / 32 + ((kBgBottom >> 8) & 0xFF) * t / 32;
        const u32 b = (kBgTop & 0xFF) * (32 - t) / 32 + (kBgBottom & 0xFF) * t / 32;
        bfill(0, static_cast<i32>(y), static_cast<i32>(g_w), 1, (r << 16) | (g << 8) | b);
    }
}

void draw_taskbar() noexcept
{
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    bfill(0, y0, static_cast<i32>(g_w), static_cast<i32>(kTaskbarH), kTaskbar);

    bfill(4, y0 + 4, 90, static_cast<i32>(kTaskbarH) - 8, kTaskBtn);
    btext(12, y0 + 8, "NOTYVOS", kTaskFg, kTaskBtn);

    i32 bx = 104;
    for (u32 i = 0; i < g_win_count; ++i)
    {
        if (!g_windows[i].visible)
            continue;
        const u32 color = g_windows[i].focused ? kTaskBtnOn : kTaskBtn;
        const u32 tw = static_cast<u32>(libk::strlen(g_windows[i].title)) * kCellW + 20;
        bfill(bx, y0 + 4, static_cast<i32>(tw), static_cast<i32>(kTaskbarH) - 8, color);
        btext(bx + 10, y0 + 8, g_windows[i].title, kTaskFg, color);
        bx += static_cast<i32>(tw) + 4;
    }

    char buf[24];
    int n = 0;
    u64 s = g_clock_sec;
    if (s == 0)
        buf[n++] = '0';
    while (s && n < 20)
    {
        buf[n++] = static_cast<char>('0' + s % 10);
        s /= 10;
    }
    buf[n++] = 's';
    for (int i = 0; i < n / 2; ++i)
    {
        char t = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = t;
    }
    buf[n] = 0;
    const i32 tw = n * static_cast<i32>(kCellW);
    const i32 ty = y0 + static_cast<i32>((kTaskbarH - kCellH) / 2);
    btext(static_cast<i32>(g_w) - tw - 12, ty, buf, kTaskFg, kTaskbar);
}

void draw_window(const Window& win) noexcept
{
    if (!win.visible)
        return;
    const u32 title = win.focused ? kTitle : kTitleOff;

    bfill(win.x - static_cast<i32>(kBorder), win.y - static_cast<i32>(kBorder),
          win.w + static_cast<i32>(kBorder) * 2, win.h + static_cast<i32>(kBorder) * 2, kBorderFg);
    bfill(win.x, win.y, win.w, static_cast<i32>(kTitleH), title);
    btext(win.x + 8, win.y + static_cast<i32>((kTitleH - kCellH) / 2), win.title, kTitleFg, title);

    const i32 cx = win.x + win.w - static_cast<i32>(kCloseBoxW) - 4;
    const i32 cy = win.y + 4;
    bfill(cx, cy, static_cast<i32>(kCloseBoxW), static_cast<i32>(kTitleH) - 8,
          win.focused ? 0x00A03030 : 0x00605060);
    btext(cx + 3, cy + 3, "X", kTitleFg, win.focused ? 0x00A03030 : 0x00605060);

    bfill(win.x, win.y + static_cast<i32>(kTitleH), win.w, win.h - static_cast<i32>(kTitleH),
          kClientBg);

    if (win.kind == WindowKind::Terminal)
    {
        const i32 gx = win.x;
        const i32 gy = win.y + static_cast<i32>(kTitleH);

        // Text
        for (u32 row = 0; row < g_term_rows; ++row)
        {
            for (u32 col = 0; col < g_term_cols; ++col)
            {
                const char ch = g_term[row * g_term_cols + col];
                if (ch == 0 || ch == ' ')
                    continue;
                bglyph(gx + static_cast<i32>(col) * static_cast<i32>(kCellW),
                       gy + static_cast<i32>(row) * static_cast<i32>(kCellH), ch, kTextFg,
                       kClientBg);
            }
        }

        // Block cursor at g_term_cursor.
        const u32 ccol = g_term_cursor % g_term_cols;
        const u32 crow = g_term_cursor / g_term_cols;
        if (crow < g_term_rows)
        {
            // Draw a solid block as an underline 3 px tall at the cell bottom.
            bfill(gx + static_cast<i32>(ccol) * static_cast<i32>(kCellW),
                  gy + static_cast<i32>(crow) * static_cast<i32>(kCellH) +
                      static_cast<i32>(kCellH) - 4,
                  static_cast<i32>(kCellW), 3, 0x00FFFFFF);
        }
    }
    else if (win.kind == WindowKind::About)
    {
        btext(win.x + 16, win.y + static_cast<i32>(kTitleH) + 16, "NOTYVOS", kTextFg, kClientBg);
        btext(win.x + 16, win.y + static_cast<i32>(kTitleH) + 16 + static_cast<i32>(kCellH),
              "Phase 3B", kTextFg, kClientBg);
        btext(win.x + 16, win.y + static_cast<i32>(kTitleH) + 16 + static_cast<i32>(kCellH) * 3,
              "Click a title bar to drag.", kTextFg, kClientBg);
        btext(win.x + 16, win.y + static_cast<i32>(kTitleH) + 16 + static_cast<i32>(kCellH) * 4,
              "Click X to close.", kTextFg, kClientBg);
    }
}

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
                bput(static_cast<u32>(x + static_cast<i32>(i) + 1),
                     static_cast<u32>(y + static_cast<i32>(j) + 1), kCursorSh);
            }
        }
    }
    for (u32 j = 0; j < 16; ++j)
    {
        const u16 bits = kArrow[j];
        for (u32 i = 0; i < 16; ++i)
        {
            if ((bits << i) & 0x8000)
            {
                bput(static_cast<u32>(x + static_cast<i32>(i)),
                     static_cast<u32>(y + static_cast<i32>(j)), kCursorFg);
            }
        }
    }
}

i32 hit_window(i32 mx, i32 my) noexcept
{
    for (i32 i = static_cast<i32>(g_win_count) - 1; i >= 0; --i)
    {
        const Window& w = g_windows[i];
        if (!w.visible)
            continue;
        if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h)
            return i;
    }
    return -1;
}

bool hit_close_box(const Window& w, i32 mx, i32 my) noexcept
{
    const i32 cx = w.x + w.w - static_cast<i32>(kCloseBoxW) - 4;
    const i32 cy = w.y + 4;
    return mx >= cx && mx < cx + static_cast<i32>(kCloseBoxW) && my >= cy &&
           my < cy + static_cast<i32>(kTitleH) - 8;
}

bool hit_title_bar(const Window& w, i32 mx, i32 my) noexcept
{
    return mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + static_cast<i32>(kTitleH);
}

void focus_window(u32 idx) noexcept
{
    if (idx >= g_win_count)
        return;
    Window tmp = g_windows[idx];
    for (u32 i = idx; i + 1 < g_win_count; ++i)
        g_windows[i] = g_windows[i + 1];
    g_windows[g_win_count - 1] = tmp;
    for (u32 i = 0; i < g_win_count; ++i)
        g_windows[i].focused = (i == g_win_count - 1);
}

void close_window(u32 idx) noexcept
{
    if (idx >= g_win_count)
        return;
    for (u32 i = idx; i + 1 < g_win_count; ++i)
        g_windows[i] = g_windows[i + 1];
    --g_win_count;
    if (g_win_count > 0)
        g_windows[g_win_count - 1].focused = true;
}

void render() noexcept
{
    if (!g_back)
        return;
    draw_background();
    for (u32 i = 0; i < g_win_count; ++i)
        draw_window(g_windows[i]);
    draw_taskbar();
    draw_cursor(arch::x86_64::mouse_x(), arch::x86_64::mouse_y());
}

void blit_to_screen() noexcept
{
    if (!g_back)
        return;
    const u32 W = fb::Framebuffer::width();
    const u32 H = fb::Framebuffer::height();
    const u32 bpp = fb::Framebuffer::bytes_per_pixel();
    auto* base = static_cast<u8*>(fb::Framebuffer::data());
    if (!base)
        return;

    const u32 copy_w = (W < g_w) ? W : g_w;
    const u32 copy_h = (H < g_h) ? H : g_h;

    for (u32 y = 0; y < copy_h; ++y)
    {
        const u32* src = g_back + y * g_w;
        u8* dst = base + static_cast<usize>(y) * fb::Framebuffer::pitch();
        if (bpp == 4)
        {
            libk::memcpy(dst, src, copy_w * 4);
        }
        else
        {
            for (u32 x = 0; x < copy_w; ++x)
            {
                const u32 c = src[x];
                if (bpp == 3)
                {
                    dst[x * 3 + 0] = static_cast<u8>(c & 0xFF);
                    dst[x * 3 + 1] = static_cast<u8>((c >> 8) & 0xFF);
                    dst[x * 3 + 2] = static_cast<u8>((c >> 16) & 0xFF);
                }
                else if (bpp == 2)
                {
                    *reinterpret_cast<u16*>(dst + x * 2) = static_cast<u16>(c & 0xFFFF);
                }
            }
        }
    }
}

void on_mouse_tick() noexcept
{
    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    const bool left = arch::x86_64::mouse_left();

    const bool just_pressed = (left && !g_prev_left);
    const bool just_released = (!left && g_prev_left);

    if (just_pressed)
    {
        const i32 idx = hit_window(mx, my);
        if (idx >= 0)
        {
            Window& w = g_windows[idx];
            if (hit_close_box(w, mx, my))
            {
                close_window(static_cast<u32>(idx));
            }
            else if (hit_title_bar(w, mx, my))
            {
                g_drag_win = idx;
                g_drag_off_x = mx - w.x;
                g_drag_off_y = my - w.y;
                focus_window(static_cast<u32>(idx));
                g_drag_win = static_cast<i32>(g_win_count - 1);
            }
            else
            {
                focus_window(static_cast<u32>(idx));
            }
        }
        else
        {
            for (u32 i = 0; i < g_win_count; ++i)
                g_windows[i].focused = false;
        }
    }

    if (just_released)
        g_drag_win = -1;

    if (g_drag_win >= 0 && left)
    {
        Window& w = g_windows[g_drag_win];
        w.x = mx - g_drag_off_x;
        w.y = my - g_drag_off_y;
        if (w.x < -w.w + 60)
            w.x = -w.w + 60;
        if (w.y < 0)
            w.y = 0;
        if (w.x > static_cast<i32>(g_w) - 60)
            w.x = static_cast<i32>(g_w) - 60;
        if (w.y > static_cast<i32>(g_h) - static_cast<i32>(kTaskbarH) - 10)
            w.y = static_cast<i32>(g_h) - static_cast<i32>(kTaskbarH) - 10;
    }

    g_prev_mx = mx;
    g_prev_my = my;
    g_prev_left = left;
}

// ---- Wallpaper loading ----
//
// Reads /wallpaper.raw from the initramfs. Format:
//   u32 width, u32 height, then width*height*4 bytes BGRA (little-endian).
// If missing, no wallpaper.

void try_load_wallpaper() noexcept
{
    if (!fs::vfs_root())
        return;
    auto* vn = fs::vfs_lookup("/wallpaper.raw", "/");
    if (!vn || !vn->ops || !vn->ops->read || !vn->ops->size)
    {
        log::write(log::Level::Info, "comp", "no wallpaper.raw; using gradient");
        return;
    }
    const isize sz = vn->ops->size(vn);
    if (sz < 16 || sz > 256 * 1024 * 1024)
    {
        log::write(log::Level::Warn, "comp", "wallpaper.raw size invalid: %lld",
                   static_cast<long long>(sz));
        return;
    }

    auto* tmp = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz)));
    if (!tmp)
        return;
    isize got = 0;
    while (got < sz)
    {
        const isize n =
            vn->ops->read(vn, tmp + got, static_cast<usize>(got), static_cast<usize>(sz - got));
        if (n <= 0)
            break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(tmp);
        return;
    }

    const u32 wp_w = *reinterpret_cast<const u32*>(tmp + 0);
    const u32 wp_h = *reinterpret_cast<const u32*>(tmp + 4);
    const u64 need = 8 + static_cast<u64>(wp_w) * wp_h * 4;
    if (need > static_cast<u64>(sz))
    {
        log::write(log::Level::Warn, "comp", "wallpaper.raw truncated");
        mm::Heap::deallocate(tmp);
        return;
    }

    // Allocate the wallpaper buffer and copy, scaling to our screen size.
    const usize wb = static_cast<usize>(g_w) * g_h * 4;
    g_wallpaper = static_cast<u32*>(mm::Heap::allocate(wb));
    if (!g_wallpaper)
    {
        mm::Heap::deallocate(tmp);
        return;
    }

    const auto* src = reinterpret_cast<const u32*>(tmp + 8);
    for (u32 y = 0; y < g_h; ++y)
    {
        const u32 sy = (y * wp_h) / g_h;
        for (u32 x = 0; x < g_w; ++x)
        {
            const u32 sx = (x * wp_w) / g_w;
            g_wallpaper[y * g_w + x] = src[sy * wp_w + sx];
        }
    }
    mm::Heap::deallocate(tmp);

    log::write(log::Level::Info, "comp", "wallpaper loaded: %llu x %llu",
               static_cast<unsigned long long>(wp_w), static_cast<unsigned long long>(wp_h));
}

} // namespace

void Compositor::init() noexcept
{
    if (!fb::Framebuffer::ready())
        return;

    g_w = fb::Framebuffer::width();
    g_h = fb::Framebuffer::height();

    const usize back_bytes = static_cast<usize>(g_w) * g_h * sizeof(u32);
    g_back = static_cast<u32*>(mm::Heap::allocate(back_bytes));
    if (!g_back)
    {
        log::write(log::Level::Error, "comp", "back buffer alloc failed (%llu bytes)",
                   static_cast<unsigned long long>(back_bytes));
        return;
    }
    libk::memset(g_back, 0, back_bytes);

    try_load_wallpaper();

    // Terminal window.
    {
        Window& t = g_windows[0];
        t.x = 40;
        t.y = 40;
        t.w = static_cast<i32>(g_w) - 80;
        t.h = static_cast<i32>(g_h) - static_cast<i32>(kTaskbarH) - 80;
        t.visible = true;
        t.focused = true;
        t.kind = WindowKind::Terminal;
        const char* s = "Terminal";
        u32 i = 0;
        while (s[i] && i < kWinTitleMax - 1)
        {
            t.title[i] = s[i];
            ++i;
        }
        t.title[i] = 0;
    }
    // About window.
    {
        Window& a = g_windows[1];
        a.x = static_cast<i32>(g_w) / 2 - 180;
        a.y = static_cast<i32>(g_h) / 2 - 120;
        a.w = 360;
        a.h = 220;
        a.visible = true;
        a.focused = false;
        a.kind = WindowKind::About;
        const char* s = "About";
        u32 i = 0;
        while (s[i] && i < kWinTitleMax - 1)
        {
            a.title[i] = s[i];
            ++i;
        }
        a.title[i] = 0;
    }
    g_win_count = 2;

    const i32 client_w = g_windows[0].w;
    const i32 client_h = g_windows[0].h - static_cast<i32>(kTitleH);
    g_term_cols = static_cast<u32>(client_w) / kCellW;
    g_term_rows = static_cast<u32>(client_h) / kCellH;
    if (g_term_cols > 120)
        g_term_cols = 120;
    if (g_term_rows > 60)
        g_term_rows = 60;
    if (g_term_cols == 0)
        g_term_cols = 1;
    if (g_term_rows == 0)
        g_term_rows = 1;

    for (u32 i = 0; i < g_term_cols * g_term_rows; ++i)
        g_term[i] = 0;
    g_term_cursor = 0;

    g_ready = true;
    g_dirty_full = true;

    render();
    blit_to_screen();
    g_dirty_full = false;

    log::write(log::Level::Info, "comp", "desktop %llu x %llu, terminal %llu x %llu",
               static_cast<unsigned long long>(g_w), static_cast<unsigned long long>(g_h),
               static_cast<unsigned long long>(g_term_cols),
               static_cast<unsigned long long>(g_term_rows));
}

bool Compositor::ready() noexcept
{
    return g_ready;
}

void Compositor::invalidate() noexcept
{
    g_dirty_full = true;
}

// Called from the boot/idle task, not from IRQs.
void Compositor::tick() noexcept
{
    if (!g_ready)
        return;
    on_mouse_tick();
    if (g_dirty_full)
    {
        render();
        blit_to_screen();
        g_dirty_full = false;
    }
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
    g_dirty_full = true;
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
        libk::memmove(g_term, g_term + g_term_cols, (g_term_rows - 1) * g_term_cols);
        for (u32 i = 0; i < g_term_cols; ++i)
        {
            g_term[(g_term_rows - 1) * g_term_cols + i] = 0;
        }
        g_term_cursor = (g_term_rows - 1) * g_term_cols;
    }
    g_dirty_full = true;
}

void Compositor::update_clock(u64 seconds) noexcept
{
    if (g_clock_sec == seconds)
        return;
    g_clock_sec = seconds;
    g_dirty_full = true;
}

} // namespace notyvos::gfx
