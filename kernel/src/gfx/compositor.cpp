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
u32* g_scene = nullptr;
u32* g_wallpaper = nullptr;

Window g_windows[kMaxWindows] = {};
u32 g_win_count = 0;

u32 g_term_cols = 0;
u32 g_term_rows = 0;
char g_term[120 * 60];
u32 g_term_cursor = 0;

u64 g_clock_sec = 0;

i32 g_cursor_x = -1000;
i32 g_cursor_y = -1000;

i32 g_prev_mx = -1;
i32 g_prev_my = -1;
bool g_prev_left = false;

i32 g_drag_win = -1;
i32 g_drag_off_x = 0;
i32 g_drag_off_y = 0;

bool g_dirty_scene = true;

inline i32 to_i32(u32 v) noexcept
{
    return static_cast<i32>(v);
}

inline void s_put(u32 x, u32 y, u32 c)
{
    if (x >= g_w || y >= g_h || !g_scene)
        return;
    g_scene[y * g_w + x] = c;
}

void s_fill(i32 x, i32 y, i32 w, i32 h, u32 c)
{
    if (!g_scene)
        return;
    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + w;
    if (x1 > to_i32(g_w))
        x1 = to_i32(g_w);
    i32 y1 = y + h;
    if (y1 > to_i32(g_h))
        y1 = to_i32(g_h);
    if (x0 >= x1 || y0 >= y1)
        return;
    for (i32 j = y0; j < y1; j++)
    {
        u32* row = g_scene + static_cast<u32>(j) * g_w;
        for (i32 i = x0; i < x1; i++)
        {
            row[static_cast<u32>(i)] = c;
        }
    }
}

void s_glyph(i32 px, i32 py, char ch, u32 fg, u32 bg)
{
    if (ch < 0x20 || ch > 0x7E)
        ch = '?';
    const u8* rows = notyvos::fb::kFont8x8[static_cast<int>(ch) - 0x20];
    for (u32 gy = 0; gy < 8; gy++)
    {
        const u8 bits = rows[gy];
        for (u32 gx = 0; gx < 8; gx++)
        {
            const bool on = ((bits >> (7 - gx)) & 1u) != 0;
            const u32 col = on ? fg : bg;
            for (u32 sy = 0; sy < kScale; sy++)
            {
                for (u32 sx = 0; sx < kScale; sx++)
                {
                    const i32 xx = px + static_cast<i32>(gx * kScale) + static_cast<i32>(sx);
                    const i32 yy = py + static_cast<i32>(gy * kScale) + static_cast<i32>(sy);
                    if (xx >= 0 && yy >= 0 && xx < to_i32(g_w) && yy < to_i32(g_h))
                    {
                        g_scene[static_cast<u32>(yy) * g_w + static_cast<u32>(xx)] = col;
                    }
                }
            }
        }
    }
}

void s_text(i32 px, i32 py, const char* s, u32 fg, u32 bg)
{
    i32 x = px;
    while (*s)
    {
        s_glyph(x, py, *s, fg, bg);
        x += static_cast<i32>(kCellW);
        s++;
    }
}

inline void fb_put(i32 x, i32 y, u32 c)
{
    if (x < 0 || y < 0 || x >= to_i32(g_w) || y >= to_i32(g_h))
        return;
    auto* base = static_cast<u8*>(fb::Framebuffer::data());
    const u32 pitch = fb::Framebuffer::pitch();
    const u32 bpp = fb::Framebuffer::bytes_per_pixel();
    u8* p = base + static_cast<usize>(y) * pitch + static_cast<usize>(x) * bpp;
    if (bpp == 4)
    {
        *reinterpret_cast<u32*>(p) = c;
    }
    else if (bpp == 3)
    {
        p[0] = static_cast<u8>(c & 0xFF);
        p[1] = static_cast<u8>((c >> 8) & 0xFF);
        p[2] = static_cast<u8>((c >> 16) & 0xFF);
    }
    else if (bpp == 2)
    {
        *reinterpret_cast<u16*>(p) = static_cast<u16>(c & 0xFFFF);
    }
}

void fb_blit_from_scene(i32 x, i32 y, i32 w, i32 h)
{
    if (!g_scene)
        return;
    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + w;
    if (x1 > to_i32(g_w))
        x1 = to_i32(g_w);
    i32 y1 = y + h;
    if (y1 > to_i32(g_h))
        y1 = to_i32(g_h);
    if (x0 >= x1 || y0 >= y1)
        return;

    auto* base = static_cast<u8*>(fb::Framebuffer::data());
    const u32 pitch = fb::Framebuffer::pitch();
    const u32 bpp = fb::Framebuffer::bytes_per_pixel();

    for (i32 j = y0; j < y1; j++)
    {
        const u32* s = g_scene + static_cast<u32>(j) * g_w + static_cast<u32>(x0);
        u8* d = base + static_cast<usize>(j) * pitch + static_cast<usize>(x0) * bpp;
        if (bpp == 4)
        {
            libk::memcpy(d, s, static_cast<usize>(x1 - x0) * 4);
        }
        else
        {
            for (i32 i = x0; i < x1; i++)
            {
                const u32 c = g_scene[static_cast<u32>(j) * g_w + static_cast<u32>(i)];
                u8* p = base + static_cast<usize>(j) * pitch + static_cast<usize>(i) * bpp;
                if (bpp == 3)
                {
                    p[0] = static_cast<u8>(c & 0xFF);
                    p[1] = static_cast<u8>((c >> 8) & 0xFF);
                    p[2] = static_cast<u8>((c >> 16) & 0xFF);
                }
                else if (bpp == 2)
                {
                    *reinterpret_cast<u16*>(p) = static_cast<u16>(c & 0xFFFF);
                }
            }
        }
    }
}

void fb_blit_full()
{
    fb_blit_from_scene(0, 0, to_i32(g_w), to_i32(g_h));
}

const u16 kArrow[16] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xF800, 0xD800, 0x0C00, 0x0C00, 0x0600, 0x0600,
};

void cursor_hide()
{
    if (g_cursor_x < 0 || g_cursor_y < 0)
        return;
    fb_blit_from_scene(g_cursor_x, g_cursor_y, 16, 16);
}

void cursor_draw(i32 x, i32 y)
{
    for (u32 j = 0; j < 16; j++)
    {
        const u16 bits = kArrow[j];
        for (u32 i = 0; i < 16; i++)
        {
            if ((bits << i) & 0x8000)
            {
                fb_put(x + static_cast<i32>(i) + 1, y + static_cast<i32>(j) + 1, kCursorSh);
                fb_put(x + static_cast<i32>(i), y + static_cast<i32>(j), kCursorFg);
            }
        }
    }
    g_cursor_x = x;
    g_cursor_y = y;
}

void scene_draw_background()
{
    if (g_wallpaper)
    {
        libk::memcpy(g_scene, g_wallpaper, static_cast<usize>(g_w) * g_h * 4);
        return;
    }
    for (u32 y = 0; y < g_h - kTaskbarH; y++)
    {
        const u32 t = (y * 32) / (g_h - kTaskbarH);
        const u32 r = ((kBgTop >> 16) & 0xFF) * (32 - t) / 32 + ((kBgBottom >> 16) & 0xFF) * t / 32;
        const u32 g = ((kBgTop >> 8) & 0xFF) * (32 - t) / 32 + ((kBgBottom >> 8) & 0xFF) * t / 32;
        const u32 b = (kBgTop & 0xFF) * (32 - t) / 32 + (kBgBottom & 0xFF) * t / 32;
        s_fill(0, static_cast<i32>(y), to_i32(g_w), 1, (r << 16) | (g << 8) | b);
    }
}

void scene_draw_taskbar()
{
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    s_fill(0, y0, to_i32(g_w), static_cast<i32>(kTaskbarH), kTaskbar);
    s_fill(4, y0 + 4, 90, static_cast<i32>(kTaskbarH) - 8, kTaskBtn);
    s_text(12, y0 + 8, "NOTYVOS", kTaskFg, kTaskBtn);

    i32 bx = 104;
    for (u32 i = 0; i < g_win_count; i++)
    {
        if (!g_windows[i].visible)
            continue;
        const u32 color = g_windows[i].focused ? kTaskBtnOn : kTaskBtn;
        const u32 tw = static_cast<u32>(libk::strlen(g_windows[i].title)) * kCellW + 20;
        s_fill(bx, y0 + 4, static_cast<i32>(tw), static_cast<i32>(kTaskbarH) - 8, color);
        s_text(bx + 10, y0 + 8, g_windows[i].title, kTaskFg, color);
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
    for (int i = 0; i < n / 2; i++)
    {
        const char t = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = t;
    }
    buf[n] = 0;
    const i32 tw = n * static_cast<i32>(kCellW);
    const i32 ty = y0 + static_cast<i32>((kTaskbarH - kCellH) / 2);
    s_text(to_i32(g_w) - tw - 12, ty, buf, kTaskFg, kTaskbar);
}

void scene_draw_window(const Window& win)
{
    if (!win.visible)
        return;
    const u32 title = win.focused ? kTitle : kTitleOff;

    s_fill(win.x - static_cast<i32>(kBorder), win.y - static_cast<i32>(kBorder),
           win.w + static_cast<i32>(kBorder) * 2, win.h + static_cast<i32>(kBorder) * 2, kBorderFg);
    s_fill(win.x, win.y, win.w, static_cast<i32>(kTitleH), title);
    s_text(win.x + 8, win.y + static_cast<i32>((kTitleH - kCellH) / 2), win.title, kTitleFg, title);

    const i32 cx = win.x + win.w - static_cast<i32>(kCloseBoxW) - 4;
    const i32 cy = win.y + 4;
    s_fill(cx, cy, static_cast<i32>(kCloseBoxW), static_cast<i32>(kTitleH) - 8,
           win.focused ? 0x00A03030 : 0x00605060);
    s_text(cx + 3, cy + 3, "X", kTitleFg, win.focused ? 0x00A03030 : 0x00605060);

    s_fill(win.x, win.y + static_cast<i32>(kTitleH), win.w, win.h - static_cast<i32>(kTitleH),
           kClientBg);

    if (win.kind == WindowKind::Terminal)
    {
        const i32 gx = win.x;
        const i32 gy = win.y + static_cast<i32>(kTitleH);
        for (u32 row = 0; row < g_term_rows; row++)
        {
            for (u32 col = 0; col < g_term_cols; col++)
            {
                const char ch = g_term[row * g_term_cols + col];
                if (ch == 0 || ch == ' ')
                    continue;
                s_glyph(gx + static_cast<i32>(col) * static_cast<i32>(kCellW),
                        gy + static_cast<i32>(row) * static_cast<i32>(kCellH), ch, kTextFg,
                        kClientBg);
            }
        }
        const u32 ccol = g_term_cursor % g_term_cols;
        const u32 crow = g_term_cursor / g_term_cols;
        if (crow < g_term_rows)
        {
            s_fill(gx + static_cast<i32>(ccol) * static_cast<i32>(kCellW),
                   gy + static_cast<i32>(crow) * static_cast<i32>(kCellH) +
                       static_cast<i32>(kCellH) - 4,
                   static_cast<i32>(kCellW), 3, 0x00FFFFFF);
        }
    }
    else if (win.kind == WindowKind::About)
    {
        const i32 bx = win.x + 16;
        const i32 by = win.y + static_cast<i32>(kTitleH) + 16;
        s_text(bx, by, "NOTYVOS", kTextFg, kClientBg);
        s_text(bx, by + static_cast<i32>(kCellH), "Phase 3B", kTextFg, kClientBg);
        s_text(bx, by + static_cast<i32>(kCellH) * 3, "Click a title bar to drag.", kTextFg,
               kClientBg);
        s_text(bx, by + static_cast<i32>(kCellH) * 4, "Click X to close.", kTextFg, kClientBg);
    }
}

void scene_render()
{
    scene_draw_background();
    for (u32 i = 0; i < g_win_count; i++)
        scene_draw_window(g_windows[i]);
    scene_draw_taskbar();
}

i32 hit_window(i32 mx, i32 my)
{
    for (i32 i = static_cast<i32>(g_win_count) - 1; i >= 0; i--)
    {
        const Window& w = g_windows[static_cast<u32>(i)];
        if (!w.visible)
            continue;
        if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h)
            return i;
    }
    return -1;
}

bool hit_close_box(const Window& w, i32 mx, i32 my)
{
    const i32 cx = w.x + w.w - static_cast<i32>(kCloseBoxW) - 4;
    const i32 cy = w.y + 4;
    return mx >= cx && mx < cx + static_cast<i32>(kCloseBoxW) && my >= cy &&
           my < cy + static_cast<i32>(kTitleH) - 8;
}

bool hit_title_bar(const Window& w, i32 mx, i32 my)
{
    return mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + static_cast<i32>(kTitleH);
}

void focus_window(u32 idx)
{
    if (idx >= g_win_count)
        return;
    const Window tmp = g_windows[idx];
    for (u32 i = idx; i + 1 < g_win_count; i++)
        g_windows[i] = g_windows[i + 1];
    g_windows[g_win_count - 1] = tmp;
    for (u32 i = 0; i < g_win_count; i++)
    {
        g_windows[i].focused = (i == g_win_count - 1);
    }
}

void close_window(u32 idx)
{
    if (idx >= g_win_count)
        return;
    for (u32 i = idx; i + 1 < g_win_count; i++)
        g_windows[i] = g_windows[i + 1];
    --g_win_count;
    if (g_win_count > 0)
        g_windows[g_win_count - 1].focused = true;
}

void on_mouse_tick()
{
    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    const bool left = arch::x86_64::mouse_left();

    const bool just_pressed = left && !g_prev_left;
    const bool just_released = !left && g_prev_left;

    if (just_pressed)
    {
        const i32 idx = hit_window(mx, my);
        if (idx >= 0)
        {
            Window& w = g_windows[static_cast<u32>(idx)];
            if (hit_close_box(w, mx, my))
            {
                close_window(static_cast<u32>(idx));
                g_dirty_scene = true;
            }
            else if (hit_title_bar(w, mx, my))
            {
                g_drag_win = idx;
                g_drag_off_x = mx - w.x;
                g_drag_off_y = my - w.y;
                focus_window(static_cast<u32>(idx));
                g_drag_win = static_cast<i32>(g_win_count) - 1;
                g_dirty_scene = true;
            }
            else
            {
                focus_window(static_cast<u32>(idx));
                g_dirty_scene = true;
            }
        }
        else
        {
            for (u32 i = 0; i < g_win_count; i++)
                g_windows[i].focused = false;
            g_dirty_scene = true;
        }
    }

    if (just_released)
        g_drag_win = -1;

    if (g_drag_win >= 0 && left)
    {
        Window& w = g_windows[static_cast<u32>(g_drag_win)];
        w.x = mx - g_drag_off_x;
        w.y = my - g_drag_off_y;
        if (w.x < -w.w + 60)
            w.x = -w.w + 60;
        if (w.y < 0)
            w.y = 0;
        if (w.x > to_i32(g_w) - 60)
            w.x = to_i32(g_w) - 60;
        if (w.y > to_i32(g_h) - static_cast<i32>(kTaskbarH) - 10)
            w.y = to_i32(g_h) - static_cast<i32>(kTaskbarH) - 10;
        g_dirty_scene = true;
    }

    g_prev_mx = mx;
    g_prev_my = my;
    g_prev_left = left;
}

void try_load_wallpaper()
{
    if (!fs::vfs_root())
        return;
    auto* vn = fs::vfs_lookup("/wallpaper.raw", "/");
    if (!vn || !vn->ops || !vn->ops->read || !vn->ops->size)
    {
        log::write(log::Level::Info, "comp", "no wallpaper.raw");
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
        mm::Heap::deallocate(tmp);
        return;
    }

    const usize wb = static_cast<usize>(g_w) * g_h * 4;
    g_wallpaper = static_cast<u32*>(mm::Heap::allocate(wb));
    if (!g_wallpaper)
    {
        mm::Heap::deallocate(tmp);
        return;
    }

    const auto* src = reinterpret_cast<const u32*>(tmp + 8);
    for (u32 y = 0; y < g_h; y++)
    {
        const u32 sy = (y * wp_h) / g_h;
        for (u32 x = 0; x < g_w; x++)
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

void Compositor::init()
{
    if (!fb::Framebuffer::ready())
        return;

    g_w = fb::Framebuffer::width();
    g_h = fb::Framebuffer::height();

    const usize scn_bytes = static_cast<usize>(g_w) * g_h * 4;
    g_scene = static_cast<u32*>(mm::Heap::allocate(scn_bytes));
    if (!g_scene)
        return;
    libk::memset(g_scene, 0, scn_bytes);

    try_load_wallpaper();

    {
        Window& t = g_windows[0];
        t.x = 40;
        t.y = 40;
        t.w = to_i32(g_w) - 80;
        t.h = to_i32(g_h) - static_cast<i32>(kTaskbarH) - 80;
        t.visible = true;
        t.focused = true;
        t.kind = WindowKind::Terminal;
        const char* s = "Terminal";
        u32 i = 0;
        while (s[i] && i < kWinTitleMax - 1)
        {
            t.title[i] = s[i];
            i++;
        }
        t.title[i] = 0;
    }
    {
        Window& a = g_windows[1];
        a.x = to_i32(g_w) / 2 - 180;
        a.y = to_i32(g_h) / 2 - 120;
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
            i++;
        }
        a.title[i] = 0;
    }
    g_win_count = 2;

    const i32 cw = g_windows[0].w;
    const i32 ch = g_windows[0].h - static_cast<i32>(kTitleH);
    g_term_cols = static_cast<u32>(cw) / kCellW;
    g_term_rows = static_cast<u32>(ch) / kCellH;
    if (g_term_cols > 120)
        g_term_cols = 120;
    if (g_term_rows > 60)
        g_term_rows = 60;
    if (g_term_cols == 0)
        g_term_cols = 1;
    if (g_term_rows == 0)
        g_term_rows = 1;

    for (u32 i = 0; i < g_term_cols * g_term_rows; i++)
        g_term[i] = 0;
    g_term_cursor = 0;

    g_ready = true;
    g_dirty_scene = true;

    scene_render();
    fb_blit_full();

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    g_cursor_x = -1000;
    cursor_draw(mx, my);

    log::write(log::Level::Info, "comp", "desktop %llu x %llu, terminal %llu x %llu",
               static_cast<unsigned long long>(g_w), static_cast<unsigned long long>(g_h),
               static_cast<unsigned long long>(g_term_cols),
               static_cast<unsigned long long>(g_term_rows));
}

bool Compositor::ready()
{
    return g_ready;
}
void Compositor::invalidate()
{
    g_dirty_scene = true;
}

void Compositor::tick()
{
    if (!g_ready)
        return;

    on_mouse_tick();

    if (g_dirty_scene)
    {
        scene_render();
        fb_blit_full();
        g_dirty_scene = false;
        g_cursor_x = -1000;
    }

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    if (mx != g_cursor_x || my != g_cursor_y)
    {
        cursor_hide();
        cursor_draw(mx, my);
    }
}

u32 Compositor::term_cols()
{
    return g_term_cols;
}
u32 Compositor::term_rows()
{
    return g_term_rows;
}

void Compositor::term_clear()
{
    for (u32 i = 0; i < g_term_cols * g_term_rows; i++)
        g_term[i] = 0;
    g_term_cursor = 0;
    g_dirty_scene = true;
}

void Compositor::term_put(char c)
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
        for (u32 i = 0; i < 4; i++)
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
        for (u32 i = 0; i < g_term_cols; i++)
        {
            g_term[(g_term_rows - 1) * g_term_cols + i] = 0;
        }
        g_term_cursor = (g_term_rows - 1) * g_term_cols;
    }
    g_dirty_scene = true;
}

void Compositor::update_clock(u64 seconds)
{
    if (g_clock_sec == seconds)
        return;
    g_clock_sec = seconds;
    g_dirty_scene = true;
}

} // namespace notyvos::gfx
