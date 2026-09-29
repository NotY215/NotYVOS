#include "../fb/font8x8.hpp"
#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/arch/x86_64/pit.hpp>
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
constexpr u32 kBtnW = 20;
constexpr u32 kBtnGap = 2;

constexpr u32 kShortcutW = 120;
constexpr u32 kShortcutH = 40;

constexpr u32 kStartW = 220;
constexpr u32 kStartItemH = 28;

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
constexpr u32 kBtnClose = 0x00A03030;
constexpr u32 kBtnMax = 0x00308050;
constexpr u32 kBtnMin = 0x00807040;
constexpr u32 kMenuBg = 0x00202838;
constexpr u32 kMenuHi = 0x00406080;
constexpr u32 kMenuFg = 0x00E0E0E0;
constexpr u32 kMenuSep = 0x00586078;

enum class ShortcutKind : u8
{
    Explorer = 0,
    Settings,
    Terminal,
    Bin
};

struct Shortcut
{
    const char* label;
    ShortcutKind kind;
    i32 x;
    i32 y;
};

Shortcut g_shortcuts[4] = {
    {"Explorer", ShortcutKind::Explorer, 32, 32},
    {"Settings", ShortcutKind::Settings, 32, 90},
    {"Terminal", ShortcutKind::Terminal, 32, 148},
    {"Bin", ShortcutKind::Bin, 32, 206},
};

// Start menu items.
enum class MenuItem : u8
{
    None = 0,
    Explorer,
    Settings,
    Terminal,
    Bin,
    Separator,
    Shutdown,
    Restart
};
const MenuItem g_menu_items[8] = {MenuItem::Explorer, MenuItem::Settings,  MenuItem::Terminal,
                                  MenuItem::Bin,      MenuItem::Separator, MenuItem::Shutdown,
                                  MenuItem::Restart,  MenuItem::None};

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
u32 g_term_scroll = 0;

u64 g_clock_sec = 0;

i32 g_cursor_x = -1000;
i32 g_cursor_y = -1000;

i32 g_prev_mx = -1;
i32 g_prev_my = -1;
bool g_prev_left = false;

i32 g_drag_win = -1;
i32 g_drag_off_x = 0;
i32 g_drag_off_y = 0;

i32 g_hover_shortcut = -1;
i32 g_focus_shortcut = -1;

i32 g_hover_menu = -1; // index into g_menu_items
bool g_start_open = false;

u64 g_last_click_tick = 0;
i32 g_last_click_shortcut = -1;

bool g_dirty_scene = true;

inline i32 to_i32(u32 v) noexcept
{
    return static_cast<i32>(v);
}

// ---------------------------------------------------------------------------
// Scene drawing primitives
// ---------------------------------------------------------------------------

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

void s_rect(i32 x, i32 y, i32 w, i32 h, u32 c)
{
    s_fill(x, y, w, 1, c);
    s_fill(x, y + h - 1, w, 1, c);
    s_fill(x, y, 1, h, c);
    s_fill(x + w - 1, y, 1, h, c);
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

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

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

void scene_draw_shortcut(const Shortcut& sc, bool hover, bool focus)
{
    const u32 bg = focus ? 0x006090C0 : hover ? 0x00406080 : 0x00203050;
    s_fill(sc.x, sc.y, static_cast<i32>(kShortcutW), static_cast<i32>(kShortcutH), bg);
    s_rect(sc.x, sc.y, static_cast<i32>(kShortcutW), static_cast<i32>(kShortcutH), kBorderFg);

    const u32 len = static_cast<u32>(libk::strlen(sc.label));
    const i32 text_w = static_cast<i32>(len) * static_cast<i32>(kCellW);
    const i32 text_x = sc.x + (static_cast<i32>(kShortcutW) - text_w) / 2;
    const i32 text_y = sc.y + (static_cast<i32>(kShortcutH) - static_cast<i32>(kCellH)) / 2;
    s_text(text_x, text_y, sc.label, kTextFg, bg);
}

void scene_draw_desktop_icons()
{
    for (u32 i = 0; i < 4; i++)
    {
        const bool hover = (static_cast<i32>(i) == g_hover_shortcut);
        const bool focus = (static_cast<i32>(i) == g_focus_shortcut);
        scene_draw_shortcut(g_shortcuts[i], hover, focus);
    }
}

void scene_draw_taskbar()
{
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    s_fill(0, y0, to_i32(g_w), static_cast<i32>(kTaskbarH), kTaskbar);

    // Start button (highlight if menu open).
    const u32 start_bg = g_start_open ? kTaskBtnOn : kTaskBtn;
    s_fill(4, y0 + 4, 100, static_cast<i32>(kTaskbarH) - 8, start_bg);
    for (i32 i = 0; i < 2; i++)
    {
        for (i32 j = 0; j < 2; j++)
        {
            s_fill(12 + i * 8, y0 + 9 + j * 8, 6, 6, kTaskFg);
        }
    }
    s_text(34, y0 + 8, "Start", kTaskFg, start_bg);

    i32 bx = 112;
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

const char* menu_label(MenuItem m)
{
    switch (m)
    {
    case MenuItem::Explorer:
        return "Explorer";
    case MenuItem::Settings:
        return "Settings";
    case MenuItem::Terminal:
        return "Terminal";
    case MenuItem::Bin:
        return "Recycle Bin";
    case MenuItem::Shutdown:
        return "Shut down";
    case MenuItem::Restart:
        return "Restart";
    default:
        return "";
    }
}

void scene_draw_start_menu()
{
    if (!g_start_open)
        return;
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    i32 menu_h = 0;
    for (u32 i = 0; g_menu_items[i] != MenuItem::None; i++)
    {
        menu_h += static_cast<i32>(kStartItemH);
    }
    const i32 mx = 4;
    const i32 my = y0 - menu_h;

    s_fill(mx, my, static_cast<i32>(kStartW), menu_h, kMenuBg);
    s_rect(mx, my, static_cast<i32>(kStartW), menu_h, kMenuSep);

    i32 cy = my;
    for (u32 i = 0; g_menu_items[i] != MenuItem::None; i++)
    {
        const MenuItem it = g_menu_items[i];
        if (it == MenuItem::Separator)
        {
            s_fill(mx + 8, cy + 2, static_cast<i32>(kStartW) - 16, 1, kMenuSep);
            cy += 6;
            continue;
        }
        const bool hover = (static_cast<i32>(i) == g_hover_menu);
        if (hover)
        {
            s_fill(mx + 2, cy + 1, static_cast<i32>(kStartW) - 4, static_cast<i32>(kStartItemH) - 2,
                   kMenuHi);
        }
        s_text(mx + 16, cy + (static_cast<i32>(kStartItemH) - static_cast<i32>(kCellH)) / 2,
               menu_label(it), kMenuFg, hover ? kMenuHi : kMenuBg);
        cy += static_cast<i32>(kStartItemH);
    }
}

void scene_draw_terminal_content(i32 gx, i32 gy)
{
    for (u32 row = 0; row < g_term_rows; row++)
    {
        i32 src_row = static_cast<i32>(row) - static_cast<i32>(g_term_scroll);
        if (src_row < 0)
            src_row = 0;
        if (src_row >= static_cast<i32>(g_term_rows))
            src_row = static_cast<i32>(g_term_rows) - 1;
        for (u32 col = 0; col < g_term_cols; col++)
        {
            const char ch = g_term[static_cast<u32>(src_row) * g_term_cols + col];
            if (ch == 0 || ch == ' ')
                continue;
            s_glyph(gx + static_cast<i32>(col) * static_cast<i32>(kCellW),
                    gy + static_cast<i32>(row) * static_cast<i32>(kCellH), ch, kTextFg, kClientBg);
        }
    }
    if (g_term_scroll == 0)
    {
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
}

void scene_draw_window_content(const Window& win, i32 gx, i32 gy, i32 gw, i32 gh)
{
    (void)gh;
    if (win.kind == WindowKind::Terminal)
    {
        scene_draw_terminal_content(gx, gy);
        return;
    }
    if (win.kind == WindowKind::Explorer)
    {
        s_text(gx + 12, gy + 12, "/", kTextFg, kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH), "  disk/", kTextFg, kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 2, "  hello.elf", kTextFg, kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 3, "  readme.txt", kTextFg, kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 4, "  hello.txt", kTextFg, kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 5, "  wallpaper.raw", kTextFg,
               kClientBg);
        (void)gw;
        return;
    }
    if (win.kind == WindowKind::Settings)
    {
        s_text(gx + 12, gy + 12, "System Settings", kTextFg, kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 2, "Display: 800x600", kTextFg,
               kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 3, "Storage: NYFS on SATA", kTextFg,
               kClientBg);
        s_text(gx + 12, gy + 12 + static_cast<i32>(kCellH) * 4, "Input: PS/2 kb + mouse", kTextFg,
               kClientBg);
        (void)gw;
        return;
    }
    if (win.kind == WindowKind::Bin)
    {
        s_text(gx + 12, gy + 12, "Recycle Bin is empty.", kTextFg, kClientBg);
        (void)gw;
        return;
    }
    s_text(gx + 12, gy + 12, win.title, kTextFg, kClientBg);
    (void)gw;
}

void scene_draw_window(const Window& win)
{
    if (!win.visible || win.minimized)
        return;
    const u32 title = win.focused ? kTitle : kTitleOff;

    s_fill(win.x - static_cast<i32>(kBorder), win.y - static_cast<i32>(kBorder),
           win.w + static_cast<i32>(kBorder) * 2, win.h + static_cast<i32>(kBorder) * 2, kBorderFg);
    s_fill(win.x, win.y, win.w, static_cast<i32>(kTitleH), title);
    s_text(win.x + 8, win.y + static_cast<i32>((kTitleH - kCellH) / 2), win.title, kTitleFg, title);

    const i32 btn_y = win.y + (static_cast<i32>(kTitleH) - 16) / 2;
    const i32 bx_close = win.x + win.w - 4 - static_cast<i32>(kBtnW);
    const i32 bx_max = bx_close - static_cast<i32>(kBtnW) - static_cast<i32>(kBtnGap);
    const i32 bx_min = bx_max - static_cast<i32>(kBtnW) - static_cast<i32>(kBtnGap);

    s_fill(bx_close, btn_y, static_cast<i32>(kBtnW), 16, kBtnClose);
    s_fill(bx_max, btn_y, static_cast<i32>(kBtnW), 16, kBtnMax);
    s_fill(bx_min, btn_y, static_cast<i32>(kBtnW), 16, kBtnMin);

    s_fill(bx_close + 5, btn_y + 4, 2, 8, kTitleFg);
    s_fill(bx_close + 11, btn_y + 4, 2, 8, kTitleFg);
    s_fill(bx_close + 5, btn_y + 4, 8, 2, kTitleFg);
    s_fill(bx_close + 5, btn_y + 10, 8, 2, kTitleFg);

    s_rect(bx_max + 5, btn_y + 4, 10, 8, kTitleFg);

    s_fill(bx_min + 5, btn_y + 7, 10, 2, kTitleFg);

    s_fill(win.x, win.y + static_cast<i32>(kTitleH), win.w, win.h - static_cast<i32>(kTitleH),
           kClientBg);

    scene_draw_window_content(win, win.x, win.y + static_cast<i32>(kTitleH), win.w,
                              win.h - static_cast<i32>(kTitleH));
}

void scene_render()
{
    scene_draw_background();
    scene_draw_desktop_icons();
    for (u32 i = 0; i < g_win_count; i++)
        scene_draw_window(g_windows[i]);
    scene_draw_taskbar();
    scene_draw_start_menu();
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

i32 hit_window(i32 mx, i32 my)
{
    for (i32 i = static_cast<i32>(g_win_count) - 1; i >= 0; i--)
    {
        const Window& w = g_windows[static_cast<u32>(i)];
        if (!w.visible || w.minimized)
            continue;
        if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h)
            return i;
    }
    return -1;
}

i32 hit_shortcut(i32 mx, i32 my)
{
    for (u32 i = 0; i < 4; i++)
    {
        const Shortcut& s = g_shortcuts[i];
        if (mx >= s.x && mx < s.x + static_cast<i32>(kShortcutW) && my >= s.y &&
            my < s.y + static_cast<i32>(kShortcutH))
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

bool hit_title_bar(const Window& w, i32 mx, i32 my)
{
    return mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + static_cast<i32>(kTitleH);
}

i32 hit_title_button(const Window& w, i32 mx, i32 my)
{
    if (my < w.y + (static_cast<i32>(kTitleH) - 16) / 2 ||
        my >= w.y + (static_cast<i32>(kTitleH) + 16) / 2)
        return 0;
    const i32 bx_close = w.x + w.w - 4 - static_cast<i32>(kBtnW);
    const i32 bx_max = bx_close - static_cast<i32>(kBtnW) - static_cast<i32>(kBtnGap);
    const i32 bx_min = bx_max - static_cast<i32>(kBtnW) - static_cast<i32>(kBtnGap);
    if (mx >= bx_close && mx < bx_close + static_cast<i32>(kBtnW))
        return 1;
    if (mx >= bx_max && mx < bx_max + static_cast<i32>(kBtnW))
        return 2;
    if (mx >= bx_min && mx < bx_min + static_cast<i32>(kBtnW))
        return 3;
    return 0;
}

bool hit_start_button(i32 mx, i32 my)
{
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    return mx >= 4 && mx < 104 && my >= y0 + 4 && my < y0 + static_cast<i32>(kTaskbarH) - 4;
}

i32 hit_menu_item(i32 mx, i32 my)
{
    if (!g_start_open)
        return -1;
    i32 menu_h = 0;
    for (u32 i = 0; g_menu_items[i] != MenuItem::None; i++)
        menu_h += static_cast<i32>(kStartItemH);
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    const i32 my_top = y0 - menu_h;
    const i32 mx_l = 4;
    const i32 mx_r = mx_l + static_cast<i32>(kStartW);
    if (mx < mx_l || mx >= mx_r || my < my_top || my >= y0)
        return -1;

    i32 cy = my_top;
    for (u32 i = 0; g_menu_items[i] != MenuItem::None; i++)
    {
        const i32 ch = (g_menu_items[i] == MenuItem::Separator) ? 6 : static_cast<i32>(kStartItemH);
        if (my >= cy && my < cy + ch)
            return static_cast<i32>(i);
        cy += ch;
    }
    return -1;
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

void toggle_maximize(u32 idx)
{
    if (idx >= g_win_count)
        return;
    Window& w = g_windows[idx];
    if (!w.maximized)
    {
        w.saved_x = w.x;
        w.saved_y = w.y;
        w.saved_w = w.w;
        w.saved_h = w.h;
        w.x = 0;
        w.y = 0;
        w.w = to_i32(g_w);
        w.h = to_i32(g_h) - static_cast<i32>(kTaskbarH);
        w.maximized = true;
    }
    else
    {
        w.x = w.saved_x;
        w.y = w.saved_y;
        w.w = w.saved_w;
        w.h = w.saved_h;
        w.maximized = false;
    }
}

void minimize_window(u32 idx)
{
    if (idx >= g_win_count)
        return;
    g_windows[idx].minimized = true;
    for (u32 i = 0; i < g_win_count; i++)
        g_windows[i].focused = false;
}

// Create a new window of a given kind, positioned in the middle.
Window* open_window(WindowKind kind, const char* title, i32 w, i32 h)
{
    if (g_win_count >= kMaxWindows)
        return nullptr;
    // If a window of this kind exists, un-minimize and focus it.
    for (u32 i = 0; i < g_win_count; i++)
    {
        if (g_windows[i].kind == kind && kind != WindowKind::Terminal)
        {
            g_windows[i].minimized = false;
            focus_window(i);
            g_dirty_scene = true;
            return &g_windows[g_win_count - 1];
        }
    }
    Window& nw = g_windows[g_win_count];
    nw.x = (to_i32(g_w) - w) / 2;
    nw.y = (to_i32(g_h) - static_cast<i32>(kTaskbarH) - h) / 2;
    nw.w = w;
    nw.h = h;
    nw.visible = true;
    nw.focused = true;
    nw.minimized = false;
    nw.maximized = false;
    nw.kind = kind;
    u32 i = 0;
    while (title[i] && i < kWinTitleMax - 1)
    {
        nw.title[i] = title[i];
        i++;
    }
    nw.title[i] = 0;
    ++g_win_count;
    focus_window(g_win_count - 1);
    g_dirty_scene = true;
    return &g_windows[g_win_count - 1];
}

void launch_shortcut(ShortcutKind kind)
{
    switch (kind)
    {
    case ShortcutKind::Explorer:
        log::write(log::Level::Info, "gfx", "launch: Explorer");
        open_window(WindowKind::Explorer, "Explorer", 360, 260);
        break;
    case ShortcutKind::Settings:
        log::write(log::Level::Info, "gfx", "launch: Settings");
        open_window(WindowKind::Settings, "Settings", 360, 220);
        break;
    case ShortcutKind::Terminal:
        log::write(log::Level::Info, "gfx", "launch: Terminal");
        for (u32 i = 0; i < g_win_count; i++)
        {
            if (g_windows[i].kind == WindowKind::Terminal)
            {
                g_windows[i].minimized = false;
                focus_window(i);
                g_dirty_scene = true;
                break;
            }
        }
        break;
    case ShortcutKind::Bin:
        log::write(log::Level::Info, "gfx", "launch: Recycle Bin");
        open_window(WindowKind::Bin, "Recycle Bin", 340, 200);
        break;
    }
}

void launch_menu_item(MenuItem m)
{
    switch (m)
    {
    case MenuItem::Explorer:
        launch_shortcut(ShortcutKind::Explorer);
        break;
    case MenuItem::Settings:
        launch_shortcut(ShortcutKind::Settings);
        break;
    case MenuItem::Terminal:
        launch_shortcut(ShortcutKind::Terminal);
        break;
    case MenuItem::Bin:
        launch_shortcut(ShortcutKind::Bin);
        break;
    case MenuItem::Shutdown:
        Compositor::machine_shutdown();
        break;
    case MenuItem::Restart:
        Compositor::machine_restart();
        break;
    default:
        break;
    }
}

void on_mouse_tick()
{
    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    const bool left = arch::x86_64::mouse_left();

    const bool just_pressed = left && !g_prev_left;
    const bool just_released = !left && g_prev_left;

    // Hover updates.
    const i32 h_shortcut = hit_shortcut(mx, my);
    if (h_shortcut != g_hover_shortcut)
    {
        g_hover_shortcut = h_shortcut;
        g_dirty_scene = true;
    }
    const i32 h_menu = hit_menu_item(mx, my);
    if (h_menu != g_hover_menu)
    {
        g_hover_menu = h_menu;
        g_dirty_scene = true;
    }

    if (just_pressed)
    {
        // Start button toggles the menu.
        if (hit_start_button(mx, my))
        {
            g_start_open = !g_start_open;
            g_hover_menu = -1;
            g_dirty_scene = true;
            g_prev_mx = mx;
            g_prev_my = my;
            g_prev_left = left;
            return;
        }

        // Menu click if open.
        if (g_start_open)
        {
            if (h_menu >= 0)
            {
                const MenuItem it = g_menu_items[static_cast<u32>(h_menu)];
                if (it != MenuItem::Separator)
                {
                    g_start_open = false;
                    launch_menu_item(it);
                    g_dirty_scene = true;
                    g_prev_mx = mx;
                    g_prev_my = my;
                    g_prev_left = left;
                    return;
                }
            }
            else
            {
                // Click outside the menu closes it.
                g_start_open = false;
                g_dirty_scene = true;
            }
        }

        const i32 idx = hit_window(mx, my);
        if (idx >= 0)
        {
            Window& w = g_windows[static_cast<u32>(idx)];
            const i32 btn = hit_title_button(w, mx, my);
            if (btn == 1)
            {
                close_window(static_cast<u32>(idx));
                g_dirty_scene = true;
            }
            else if (btn == 2)
            {
                toggle_maximize(static_cast<u32>(idx));
                g_dirty_scene = true;
            }
            else if (btn == 3)
            {
                minimize_window(static_cast<u32>(idx));
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
        else if (h_shortcut >= 0)
        {
            const u64 now = arch::x86_64::pit_ticks();
            const bool dbl =
                (h_shortcut == g_last_click_shortcut) && ((now - g_last_click_tick) < 50);
            g_last_click_shortcut = h_shortcut;
            g_last_click_tick = now;
            if (dbl)
            {
                launch_shortcut(g_shortcuts[static_cast<u32>(h_shortcut)].kind);
                g_last_click_shortcut = -1;
            }
            g_focus_shortcut = h_shortcut;
            g_dirty_scene = true;
        }
        else
        {
            // Taskbar window buttons.
            const i32 ty = to_i32(g_h) - static_cast<i32>(kTaskbarH);
            if (my >= ty && my < to_i32(g_h))
            {
                i32 bx = 112;
                for (u32 i = 0; i < g_win_count; i++)
                {
                    if (!g_windows[i].visible)
                        continue;
                    const u32 tw = static_cast<u32>(libk::strlen(g_windows[i].title)) * kCellW + 20;
                    if (mx >= bx && mx < bx + static_cast<i32>(tw))
                    {
                        g_windows[i].minimized = false;
                        focus_window(i);
                        g_dirty_scene = true;
                        break;
                    }
                    bx += static_cast<i32>(tw) + 4;
                }
            }
            else
            {
                for (u32 i = 0; i < g_win_count; i++)
                {
                    g_windows[i].focused = false;
                }
                g_focus_shortcut = -1;
                g_dirty_scene = true;
            }
        }
    }

    if (just_released)
    {
        g_drag_win = -1;
    }

    if (g_drag_win >= 0 && left)
    {
        Window& w = g_windows[static_cast<u32>(g_drag_win)];
        if (w.maximized)
        {
            w.maximized = false;
        }
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

// ---------------------------------------------------------------------------
// Power actions
// ---------------------------------------------------------------------------

void Compositor::machine_shutdown() noexcept
{
    log::write(log::Level::Info, "power", "shutdown requested");
    // Try the common ACPI / Bochs / QEMU shutdown ports.
    asm volatile("outw %0, %1" ::"a"(static_cast<u16>(0x2000)), "Nd"(static_cast<u16>(0x604)));
    asm volatile("outw %0, %1" ::"a"(static_cast<u16>(0x2000)), "Nd"(static_cast<u16>(0xB004)));
    asm volatile("outw %0, %1" ::"a"(static_cast<u16>(0x3400)), "Nd"(static_cast<u16>(0x4004)));
    // Fallback: keyboard controller reset. VirtualBox will reboot.
    asm volatile("outb %0, %1" ::"a"(static_cast<u8>(0xFE)), "Nd"(static_cast<u16>(0x64)));
    for (;;)
        asm volatile("hlt");
}

void Compositor::machine_restart() noexcept
{
    log::write(log::Level::Info, "power", "restart requested");
    // Keyboard controller pulse reset line.
    asm volatile("outb %0, %1" ::"a"(static_cast<u8>(0xFE)), "Nd"(static_cast<u16>(0x64)));
    for (;;)
        asm volatile("hlt");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Compositor::init() noexcept
{
    if (!fb::Framebuffer::ready())
        return;

    g_w = fb::Framebuffer::width();
    g_h = fb::Framebuffer::height();

    const usize scn_bytes = static_cast<usize>(g_w) * g_h * 4;
    g_scene = static_cast<u32*>(mm::Heap::allocate(scn_bytes));
    if (!g_scene)
    {
        log::write(log::Level::Error, "comp", "scene alloc failed");
        return;
    }
    libk::memset(g_scene, 0, scn_bytes);

    try_load_wallpaper();

    {
        Window& t = g_windows[0];
        t.x = 180;
        t.y = 40;
        t.w = to_i32(g_w) - 220;
        t.h = to_i32(g_h) - static_cast<i32>(kTaskbarH) - 80;
        t.visible = true;
        t.focused = true;
        t.minimized = false;
        t.maximized = false;
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
    g_win_count = 1;

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
    g_term_scroll = 0;

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

bool Compositor::ready() noexcept
{
    return g_ready;
}
void Compositor::invalidate() noexcept
{
    g_dirty_scene = true;
}

void Compositor::tick() noexcept
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

u32 Compositor::term_cols() noexcept
{
    return g_term_cols;
}
u32 Compositor::term_rows() noexcept
{
    return g_term_rows;
}

void Compositor::term_scroll_by(i32 delta) noexcept
{
    if (delta > 0)
    {
        if (g_term_scroll + static_cast<u32>(delta) < g_term_rows)
        {
            g_term_scroll += static_cast<u32>(delta);
        }
        else
        {
            g_term_scroll = g_term_rows - 1;
        }
    }
    else if (delta < 0)
    {
        const u32 mag = static_cast<u32>(-delta);
        if (mag >= g_term_scroll)
            g_term_scroll = 0;
        else
            g_term_scroll -= mag;
    }
    g_dirty_scene = true;
}

void Compositor::term_scroll_bottom() noexcept
{
    g_term_scroll = 0;
    g_dirty_scene = true;
}

void Compositor::term_clear() noexcept
{
    for (u32 i = 0; i < g_term_cols * g_term_rows; i++)
        g_term[i] = 0;
    g_term_cursor = 0;
    g_term_scroll = 0;
    g_dirty_scene = true;
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
    g_term_scroll = 0;
    g_dirty_scene = true;
}

void Compositor::update_clock(u64 seconds) noexcept
{
    if (g_clock_sec == seconds)
        return;
    g_clock_sec = seconds;
    g_dirty_scene = true;
}

} // namespace notyvos::gfx
