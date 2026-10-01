#include "../fb/font8x8.hpp"
#include <kernel/acpi/acpi.hpp>
#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/arch/x86_64/rtc.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/gfx/api.hpp>
#include <kernel/gfx/apps.hpp>
#include <kernel/gfx/backend_vbe.hpp>
#include <kernel/gfx/compositor.hpp>
#include <kernel/gfx/hal.hpp>
#include <kernel/gfx/theme.hpp>
#include <kernel/gpu/gpu.hpp>
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
constexpr u32 kShortcutW = 120;
constexpr u32 kShortcutH = 40;
constexpr u32 kCtxW = 180;
constexpr u32 kCtxItemH = 24;
constexpr u32 kCtxSepH = 6;

// Theme-driven palette.
u32 kBgTop, kBgBottom;
u32 kTaskbarBg, kTaskbarHi;
u32 kTitleOn, kTitleOff;
u32 kTitleFg, kTitleFgOff;
u32 kBorderFg, kBorderOn;
u32 kClientBg, kTextFg, kTextDim;
u32 kCursorFg, kCursorSh;
u32 kBtnClose, kBtnHover;
u32 kAccent;
u32 kMenuBg, kMenuHi, kMenuFg, kMenuSep;
u32 kShortcutBg, kShortcutHover, kShortcutFocus;

void apply_theme() noexcept
{
    const auto& p = theme::current();
    kBgTop = p.desktop_top;
    kBgBottom = p.desktop_bottom;
    kTaskbarBg = p.taskbar_bg;
    kTaskbarHi = p.taskbar_hi;
    kTitleOn = p.title_focused;
    kTitleOff = p.title_unfocused;
    kTitleFg = p.title_fg_focused;
    kTitleFgOff = p.title_fg_unfocused;
    kBorderFg = p.border_unfocused;
    kBorderOn = p.border_focused;
    kClientBg = p.client_bg;
    kTextFg = p.text_fg;
    kTextDim = p.text_dim;
    kCursorFg = p.cursor_fg;
    kCursorSh = p.cursor_shadow;
    kBtnClose = p.btn_close;
    kBtnHover = p.btn_hover;
    kAccent = p.accent;
    kMenuBg = p.menu_bg;
    kMenuHi = p.menu_hi;
    kMenuFg = p.menu_fg;
    kMenuSep = p.menu_sep;
    kShortcutBg = p.shortcut_bg;
    kShortcutHover = p.shortcut_hover;
    kShortcutFocus = p.shortcut_focus;
}

enum class ShortcutKind : u8
{
    Explorer = 0,
    Settings,
    Terminal,
    GameLchr,
    ImageVw,
    Bin
};

struct Shortcut
{
    const char* label;
    ShortcutKind kind;
    i32 x;
    i32 y;
};

Shortcut g_shortcuts[6] = {
    {"Explorer", ShortcutKind::Explorer, 32, 32},  {"Settings", ShortcutKind::Settings, 32, 90},
    {"Terminal", ShortcutKind::Terminal, 32, 148}, {"Game Lchr", ShortcutKind::GameLchr, 32, 206},
    {"Images", ShortcutKind::ImageVw, 32, 264},    {"Bin", ShortcutKind::Bin, 32, 322},
};

enum class MenuItem : u8
{
    None = 0,
    Explorer,
    Settings,
    Terminal,
    GameLchr,
    ImageVw,
    Bin,
    Shutdown,
    Restart
};

enum class CtxItem : u8
{
    None = 0,
    New,
    Refresh,
    Sep1,
    Cut,
    Copy,
    Paste,
    Sep2,
    Rename,
    Properties
};
const CtxItem g_ctx_items[10] = {
    CtxItem::New,   CtxItem::Refresh, CtxItem::Sep1,   CtxItem::Cut,        CtxItem::Copy,
    CtxItem::Paste, CtxItem::Sep2,    CtxItem::Rename, CtxItem::Properties, CtxItem::None};

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
u64 g_rtc_boot_unix = 0;
u64 g_rtc_boot_ticks = 0;

i32 g_cursor_x = -1000;
i32 g_cursor_y = -1000;

i32 g_prev_mx = -1;
i32 g_prev_my = -1;
bool g_prev_left = false;
bool g_prev_right = false;

i32 g_drag_win = -1;
i32 g_drag_off_x = 0;
i32 g_drag_off_y = 0;

i32 g_hover_shortcut = -1;
i32 g_focus_shortcut = -1;

i32 g_hover_menu = -1;
bool g_start_open = false;

bool g_ctx_open = false;
i32 g_ctx_x = 0;
i32 g_ctx_y = 0;
i32 g_ctx_hover = -1;

u64 g_last_click_tick = 0;
i32 g_last_click_shortcut = -1;

bool g_dirty_scene = true;

inline i32 to_i32(u32 v) noexcept
{
    return static_cast<i32>(v);
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
    for (i32 j = y0; j < y1; ++j)
    {
        u32* row = g_scene + static_cast<u32>(j) * g_w;
        for (i32 i = x0; i < x1; ++i)
            row[static_cast<u32>(i)] = c;
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
    for (u32 gy = 0; gy < 8; ++gy)
    {
        const u8 bits = rows[gy];
        for (u32 gx = 0; gx < 8; ++gx)
        {
            const bool on = ((bits >> (7 - gx)) & 1u) != 0;
            const u32 col = on ? fg : bg;
            for (u32 sy = 0; sy < kScale; ++sy)
                for (u32 sx = 0; sx < kScale; ++sx)
                {
                    const i32 xx = px + static_cast<i32>(gx * kScale + sx);
                    const i32 yy = py + static_cast<i32>(gy * kScale + sy);
                    if (xx >= 0 && yy >= 0 && xx < to_i32(g_w) && yy < to_i32(g_h))
                        g_scene[static_cast<u32>(yy) * g_w + static_cast<u32>(xx)] = col;
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
        ++s;
    }
}

void widget_rect_cb(i32 x, i32 y, i32 w, i32 h, u32 c)
{
    s_fill(x, y, w, h, c);
}
void widget_text_cb(i32 x, i32 y, const char* s, u32 fg, u32 bg)
{
    s_text(x, y, s, fg, bg);
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
        *reinterpret_cast<u32*>(p) = c;
    else if (bpp == 3)
    {
        p[0] = static_cast<u8>(c & 0xFFu);
        p[1] = static_cast<u8>((c >> 8) & 0xFFu);
        p[2] = static_cast<u8>((c >> 16) & 0xFFu);
    }
    else if (bpp == 2)
        *reinterpret_cast<u16*>(p) = static_cast<u16>(c & 0xFFFFu);
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

    for (i32 j = y0; j < y1; ++j)
    {
        const u32* s = g_scene + static_cast<u32>(j) * g_w + static_cast<u32>(x0);
        if (bpp == 4)
        {
            u8* d = base + static_cast<usize>(j) * pitch + static_cast<usize>(x0) * 4;
            libk::memcpy(d, s, static_cast<usize>(x1 - x0) * 4);
        }
        else
        {
            for (i32 i = x0; i < x1; ++i)
            {
                const u32 c = g_scene[static_cast<u32>(j) * g_w + static_cast<u32>(i)];
                u8* p = base + static_cast<usize>(j) * pitch + static_cast<usize>(i) * bpp;
                if (bpp == 3)
                {
                    p[0] = static_cast<u8>(c & 0xFFu);
                    p[1] = static_cast<u8>((c >> 8) & 0xFFu);
                    p[2] = static_cast<u8>((c >> 16) & 0xFFu);
                }
                else if (bpp == 2)
                    *reinterpret_cast<u16*>(p) = static_cast<u16>(c & 0xFFFFu);
            }
        }
    }
}

void fb_blit_full()
{
    if (!g_scene)
        return;
    vbe_blit(g_scene, g_w, g_h, g_w, 0, 0, g_w, g_h);
}

const u16 kArrow[16] = {0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
                        0xFF80, 0xFFC0, 0xF800, 0xD800, 0x0C00, 0x0C00, 0x0600, 0x0600};

void cursor_hide()
{
    if (g_cursor_x < 0 || g_cursor_y < 0)
        return;
    fb_blit_from_scene(g_cursor_x, g_cursor_y, 16, 16);
}

void cursor_draw(i32 x, i32 y)
{
    for (u32 j = 0; j < 16; ++j)
    {
        const u16 bits = kArrow[j];
        for (u32 i = 0; i < 16; ++i)
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
    for (u32 y = 0; y < g_h - kTaskbarH; ++y)
    {
        const u32 t = (y * 32) / (g_h - kTaskbarH);
        const u32 r =
            ((kBgTop >> 16) & 0xFFu) * (32 - t) / 32 + ((kBgBottom >> 16) & 0xFFu) * t / 32;
        const u32 g = ((kBgTop >> 8) & 0xFFu) * (32 - t) / 32 + ((kBgBottom >> 8) & 0xFFu) * t / 32;
        const u32 b = (kBgTop & 0xFFu) * (32 - t) / 32 + (kBgBottom & 0xFFu) * t / 32;
        s_fill(0, static_cast<i32>(y), to_i32(g_w), 1, (r << 16) | (g << 8) | b);
    }
}

void scene_draw_shortcut(const Shortcut& sc, bool hover, bool focus)
{
    const u32 bg = focus ? kShortcutFocus : hover ? kShortcutHover : kShortcutBg;
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
    for (u32 i = 0; i < 6; ++i)
    {
        const bool hover = (static_cast<i32>(i) == g_hover_shortcut);
        const bool focus = (static_cast<i32>(i) == g_focus_shortcut);
        scene_draw_shortcut(g_shortcuts[i], hover, focus);
    }
}

i32 taskbar_cluster_x()
{
    const i32 center = to_i32(g_w) / 2;
    i32 cluster_w = 44 + 44;
    for (u32 i = 0; i < g_win_count; ++i)
    {
        if (!g_windows[i].visible)
            continue;
        const u32 tw = static_cast<u32>(libk::strlen(g_windows[i].title)) * kCellW + 20;
        cluster_w += static_cast<i32>(tw) + 6;
    }
    return center - cluster_w / 2;
}

namespace
{
void civil_from_unix(u64 unix_secs, u32& year, u32& month, u32& day, u32& hour, u32& minute)
{
    const u64 days = unix_secs / 86400u;
    const u64 rem = unix_secs % 86400u;
    hour = static_cast<u32>(rem / 3600u);
    minute = static_cast<u32>((rem / 60u) % 60u);

    const i64 z = static_cast<i64>(days) + 719468;
    const i64 era = (z >= 0 ? z : z - 146096) / 146097;
    const u32 doe = static_cast<u32>(z - era * 146097);
    const u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    i64 y = static_cast<i64>(yoe) + era * 400;
    const u32 doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const u32 mp = (5u * doy + 2u) / 153u;
    day = doy - (153u * mp + 2u) / 5u + 1u;
    month = mp + (mp < 10u ? 3u : static_cast<u32>(-9));
    y += (month <= 2) ? 1 : 0;
    year = static_cast<u32>(y);
}
} // namespace

void scene_draw_taskbar()
{
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    s_fill(0, y0, to_i32(g_w), static_cast<i32>(kTaskbarH), kTaskbarBg);

    const i32 start_x = taskbar_cluster_x();
    i32 x = start_x;

    {
        const bool open = g_start_open;
        const u32 bg = open ? kAccent : kTaskbarBg;
        s_fill(x, y0 + 4, 36, static_cast<i32>(kTaskbarH) - 8, bg);
        for (i32 i = 0; i < 2; ++i)
            for (i32 j = 0; j < 2; ++j)
                s_fill(x + 8 + i * 10, y0 + 10 + j * 10, 8, 8, kTextFg);
        x += 44;
    }
    {
        s_fill(x, y0 + 4, 36, static_cast<i32>(kTaskbarH) - 8, kTaskbarBg);
        for (i32 i = 0; i < 10; ++i)
        {
            s_fill(x + 10 + i, y0 + 10, 1, 1, kTextDim);
            s_fill(x + 10 + i, y0 + 19, 1, 1, kTextDim);
            s_fill(x + 10, y0 + 10 + i, 1, 1, kTextDim);
            s_fill(x + 19, y0 + 10 + i, 1, 1, kTextDim);
        }
        s_fill(x + 20, y0 + 20, 5, 2, kTextDim);
        s_fill(x + 24, y0 + 22, 2, 5, kTextDim);
        x += 44;
    }

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();

    for (u32 i = 0; i < g_win_count; ++i)
    {
        if (!g_windows[i].visible)
            continue;
        const u32 tw = static_cast<u32>(libk::strlen(g_windows[i].title)) * kCellW + 20;
        const bool hover = (mx >= x && mx < x + static_cast<i32>(tw) && my >= y0 + 4 &&
                            my < y0 + static_cast<i32>(kTaskbarH) - 4);
        const u32 color = g_windows[i].focused ? kAccent : hover ? kTaskbarHi : kTaskbarBg;
        s_fill(x, y0 + 4, static_cast<i32>(tw), static_cast<i32>(kTaskbarH) - 8, color);
        if (g_windows[i].focused)
            s_fill(x + 8, y0 + static_cast<i32>(kTaskbarH) - 4, static_cast<i32>(tw) - 16, 2,
                   kAccent);
        s_text(x + 10, y0 + 8, g_windows[i].title, kTitleFg, color);
        x += static_cast<i32>(tw) + 6;
    }

    const i32 tray_x = to_i32(g_w) - 130;
    for (i32 k = 0; k < 3; ++k)
        s_fill(tray_x + k * 20, y0 + 12, 6, 6, kTextDim);

    {
        u32 yy = 0, mo = 0, dy = 0, hh = 0, mi = 0;
        civil_from_unix(g_clock_sec, yy, mo, dy, hh, mi);
        char buf[16];
        int n = 0;
        auto push2 = [&](u32 v)
        {
            buf[n++] = static_cast<char>('0' + (v / 10) % 10);
            buf[n++] = static_cast<char>('0' + v % 10);
        };
        push2(hh);
        buf[n++] = ':';
        push2(mi);
        buf[n] = 0;
        s_text(tray_x + 68, y0 + 4, buf, kTextFg, kTaskbarBg);
    }
    {
        u32 yy = 0, mo = 0, dy = 0, hh = 0, mi = 0;
        civil_from_unix(g_clock_sec, yy, mo, dy, hh, mi);
        char buf[16];
        int n = 0;
        auto push2 = [&](u32 v)
        {
            buf[n++] = static_cast<char>('0' + (v / 10) % 10);
            buf[n++] = static_cast<char>('0' + v % 10);
        };
        push2(dy);
        buf[n++] = '/';
        push2(mo);
        buf[n++] = '/';
        push2(yy % 100u);
        buf[n] = 0;
        s_text(tray_x + 68, y0 + 16, buf, kTextDim, kTaskbarBg);
    }
}

void scene_draw_start_menu()
{
    if (!g_start_open)
        return;

    const i32 menu_w = 380;
    const i32 item_h = 40;
    const i32 header_h = 36;
    const i32 menu_h = header_h + item_h * 7 + 20;

    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    const i32 mx = to_i32(g_w) / 2 - menu_w / 2;
    const i32 my = y0 - menu_h;

    s_fill(mx, my, menu_w, menu_h, kMenuBg);
    s_rect(mx, my, menu_w, menu_h, kMenuSep);

    s_fill(mx + 1, my + 1, menu_w - 2, header_h - 2, kMenuBg);
    s_text(mx + 40, my + 10, "NOTYVOS user", kMenuFg, kMenuBg);

    const char* items[7] = {"Explorer",     "Settings",    "Terminal", "Game Launcher",
                            "Image Viewer", "Recycle Bin", "Shut down"};

    i32 cy = my + header_h + 4;
    for (u32 i = 0; i < 7; ++i)
    {
        const bool hover = (static_cast<i32>(i) == g_hover_menu);
        const u32 bg = hover ? kMenuHi : kMenuBg;
        if (hover)
            s_fill(mx + 4, cy, menu_w - 8, item_h - 4, bg);
        s_text(mx + 20, cy + 12, items[i], kMenuFg, bg);
        cy += item_h;
    }
}

const char* ctx_label(CtxItem it)
{
    switch (it)
    {
    case CtxItem::New:
        return "New";
    case CtxItem::Refresh:
        return "Refresh";
    case CtxItem::Cut:
        return "Cut";
    case CtxItem::Copy:
        return "Copy";
    case CtxItem::Paste:
        return "Paste";
    case CtxItem::Rename:
        return "Rename";
    case CtxItem::Properties:
        return "Properties";
    default:
        return "";
    }
}

i32 ctx_menu_height()
{
    i32 h = 0;
    for (u32 i = 0; g_ctx_items[i] != CtxItem::None; ++i)
        h += (g_ctx_items[i] == CtxItem::Sep1 || g_ctx_items[i] == CtxItem::Sep2)
                 ? static_cast<i32>(kCtxSepH)
                 : static_cast<i32>(kCtxItemH);
    return h;
}

void scene_draw_context_menu()
{
    if (!g_ctx_open)
        return;
    const i32 h = ctx_menu_height();

    i32 x = g_ctx_x, y = g_ctx_y;
    if (x + static_cast<i32>(kCtxW) > to_i32(g_w))
        x = to_i32(g_w) - static_cast<i32>(kCtxW);
    if (y + h > to_i32(g_h) - static_cast<i32>(kTaskbarH))
        y = to_i32(g_h) - static_cast<i32>(kTaskbarH) - h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    g_ctx_x = x;
    g_ctx_y = y;

    s_fill(x, y, static_cast<i32>(kCtxW), h, kMenuBg);
    s_rect(x, y, static_cast<i32>(kCtxW), h, kMenuSep);

    i32 cy = y;
    for (u32 i = 0; g_ctx_items[i] != CtxItem::None; ++i)
    {
        const CtxItem it = g_ctx_items[i];
        if (it == CtxItem::Sep1 || it == CtxItem::Sep2)
        {
            s_fill(x + 8, cy + 2, static_cast<i32>(kCtxW) - 16, 1, kMenuSep);
            cy += static_cast<i32>(kCtxSepH);
            continue;
        }
        const bool hover = (static_cast<i32>(i) == g_ctx_hover);
        if (hover)
            s_fill(x + 2, cy + 1, static_cast<i32>(kCtxW) - 4, static_cast<i32>(kCtxItemH) - 2,
                   kMenuHi);
        s_text(x + 16, cy + (static_cast<i32>(kCtxItemH) - static_cast<i32>(kCellH)) / 2,
               ctx_label(it), kMenuFg, hover ? kMenuHi : kMenuBg);
        cy += static_cast<i32>(kCtxItemH);
    }
}

void scene_draw_terminal_content(i32 gx, i32 gy)
{
    for (u32 row = 0; row < g_term_rows; ++row)
    {
        i32 src_row = static_cast<i32>(row) - static_cast<i32>(g_term_scroll);
        if (src_row < 0)
            src_row = 0;
        if (src_row >= static_cast<i32>(g_term_rows))
            src_row = static_cast<i32>(g_term_rows) - 1;
        for (u32 col = 0; col < g_term_cols; ++col)
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
                   static_cast<i32>(kCellW), 3, kCursorFg);
        }
    }
}

void scene_draw_window_content(const Window& win, i32 gx, i32 gy, i32 gw, i32 gh)
{
    if (win.kind == WindowKind::Terminal)
    {
        scene_draw_terminal_content(gx, gy);
        return;
    }
    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    const bool down = arch::x86_64::mouse_left();

    if (win.kind == WindowKind::Explorer)
    {
        apps_draw_explorer(gx, gy, gw, gh, mx, my, down);
        return;
    }
    if (win.kind == WindowKind::Settings)
    {
        apps_draw_settings(gx, gy, gw, gh, mx, my, down);
        return;
    }
    if (win.kind == WindowKind::Bin)
    {
        apps_draw_bin(gx, gy, gw, gh, mx, my, down);
        return;
    }
    if (win.kind == WindowKind::ImageViewer)
    {
        apps_draw_imageviewer(gx, gy, gw, gh, mx, my, down);
        return;
    }
    s_text(gx + 12, gy + 12, win.title, kTextFg, kClientBg);
}

void clear_corner(i32 x, i32 y)
{
    if (x < 0 || y < 0 || x >= to_i32(g_w) || y >= to_i32(g_h))
        return;
    s_fill(x, y, 1, 1, kBgBottom);
}

void scene_draw_window(const Window& win)
{
    if (!win.visible || win.minimized)
        return;

    const u32 title = win.focused ? kTitleOn : kTitleOff;
    const u32 titleFg = win.focused ? kTitleFg : kTitleFgOff;
    const u32 border = win.focused ? kBorderOn : kBorderFg;

    s_fill(win.x - static_cast<i32>(kBorder), win.y - static_cast<i32>(kBorder),
           win.w + static_cast<i32>(kBorder) * 2, static_cast<i32>(kBorder), border);
    s_fill(win.x - static_cast<i32>(kBorder), win.y + win.h, win.w + static_cast<i32>(kBorder) * 2,
           static_cast<i32>(kBorder), border);
    s_fill(win.x - static_cast<i32>(kBorder), win.y, static_cast<i32>(kBorder), win.h, border);
    s_fill(win.x + win.w, win.y, static_cast<i32>(kBorder), win.h, border);

    clear_corner(win.x - static_cast<i32>(kBorder), win.y - static_cast<i32>(kBorder));
    clear_corner(win.x + win.w, win.y - static_cast<i32>(kBorder));
    clear_corner(win.x - static_cast<i32>(kBorder), win.y + win.h);
    clear_corner(win.x + win.w, win.y + win.h);

    s_fill(win.x, win.y, win.w, static_cast<i32>(kTitleH), title);
    if (win.focused)
        s_fill(win.x, win.y, win.w, 2, kAccent);

    s_text(win.x + 10, win.y + static_cast<i32>((kTitleH - kCellH) / 2), win.title, titleFg, title);
    (void)kTextDim;

    const i32 btn_y = win.y + 1;
    const i32 btn_h = static_cast<i32>(kTitleH) - 2;
    const i32 btn_w = 32;
    const i32 bx_close = win.x + win.w - btn_w;
    const i32 bx_max = bx_close - btn_w;
    const i32 bx_min = bx_max - btn_w;

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();

    const bool close_hover =
        (mx >= bx_close && mx < bx_close + btn_w && my >= btn_y && my < btn_y + btn_h);
    s_fill(bx_close, btn_y, btn_w, btn_h, close_hover ? kBtnClose : title);
    {
        const i32 gx = bx_close + btn_w / 2 - 5;
        const i32 gy = btn_y + btn_h / 2 - 5;
        for (i32 i = 0; i < 10; ++i)
        {
            s_fill(gx + i, gy + i, 1, 1, kTitleFg);
            s_fill(gx + 9 - i, gy + i, 1, 1, kTitleFg);
        }
    }

    const bool max_hover =
        (mx >= bx_max && mx < bx_max + btn_w && my >= btn_y && my < btn_y + btn_h);
    s_fill(bx_max, btn_y, btn_w, btn_h, max_hover ? kBtnHover : title);
    {
        const i32 gx = bx_max + btn_w / 2 - 5;
        const i32 gy = btn_y + btn_h / 2 - 5;
        s_fill(gx, gy, 11, 1, kTitleFg);
        s_fill(gx, gy + 10, 11, 1, kTitleFg);
        s_fill(gx, gy, 1, 11, kTitleFg);
        s_fill(gx + 10, gy, 1, 11, kTitleFg);
    }

    const bool min_hover =
        (mx >= bx_min && mx < bx_min + btn_w && my >= btn_y && my < btn_y + btn_h);
    s_fill(bx_min, btn_y, btn_w, btn_h, min_hover ? kBtnHover : title);
    {
        const i32 gx = bx_min + btn_w / 2 - 5;
        const i32 gy = btn_y + btn_h / 2;
        s_fill(gx, gy, 11, 1, kTitleFg);
    }

    s_fill(win.x, win.y + static_cast<i32>(kTitleH), win.w, win.h - static_cast<i32>(kTitleH),
           kClientBg);

    scene_draw_window_content(win, win.x, win.y + static_cast<i32>(kTitleH), win.w,
                              win.h - static_cast<i32>(kTitleH));
}

void scene_render()
{
    scene_draw_background();
    scene_draw_desktop_icons();
    for (u32 i = 0; i < g_win_count; ++i)
        scene_draw_window(g_windows[i]);
    scene_draw_taskbar();
    scene_draw_start_menu();
    scene_draw_context_menu();
}

i32 hit_window(i32 mx, i32 my)
{
    for (i32 i = static_cast<i32>(g_win_count) - 1; i >= 0; --i)
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
    for (u32 i = 0; i < 6; ++i)
    {
        const Shortcut& s = g_shortcuts[i];
        if (mx >= s.x && mx < s.x + static_cast<i32>(kShortcutW) && my >= s.y &&
            my < s.y + static_cast<i32>(kShortcutH))
            return static_cast<i32>(i);
    }
    return -1;
}

bool hit_title_bar(const Window& w, i32 mx, i32 my)
{
    return mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + static_cast<i32>(kTitleH);
}

i32 hit_title_button(const Window& w, i32 mx, i32 my)
{
    if (my < w.y + 1)
        return 0;
    if (my >= w.y + static_cast<i32>(kTitleH) - 1)
        return 0;
    const i32 btn_w = 32;
    const i32 bx_close = w.x + w.w - btn_w;
    const i32 bx_max = bx_close - btn_w;
    const i32 bx_min = bx_max - btn_w;
    if (mx >= bx_close && mx < bx_close + btn_w)
        return 1;
    if (mx >= bx_max && mx < bx_max + btn_w)
        return 2;
    if (mx >= bx_min && mx < bx_min + btn_w)
        return 3;
    return 0;
}

bool hit_start_button(i32 mx, i32 my)
{
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    const i32 start_x = taskbar_cluster_x();
    return mx >= start_x && mx < start_x + 36 && my >= y0 + 4 &&
           my < y0 + static_cast<i32>(kTaskbarH) - 4;
}

i32 hit_menu_item(i32 mx, i32 my)
{
    if (!g_start_open)
        return -1;
    const i32 menu_w = 380;
    const i32 item_h = 40;
    const i32 header_h = 36;
    const i32 menu_h = header_h + item_h * 7 + 20;
    const i32 y0 = static_cast<i32>(g_h - kTaskbarH);
    const i32 mx_l = to_i32(g_w) / 2 - menu_w / 2;
    const i32 my_top = y0 - menu_h;
    if (mx < mx_l || mx >= mx_l + menu_w || my < my_top || my >= y0)
        return -1;
    const i32 first = my_top + header_h + 4;
    if (my < first)
        return -1;
    const i32 idx = (my - first) / item_h;
    if (idx < 0 || idx >= 7)
        return -1;
    return idx;
}

i32 hit_ctx_item(i32 mx, i32 my)
{
    if (!g_ctx_open)
        return -1;
    if (mx < g_ctx_x || mx >= g_ctx_x + static_cast<i32>(kCtxW))
        return -1;
    if (my < g_ctx_y)
        return -1;
    i32 cy = g_ctx_y;
    for (u32 i = 0; g_ctx_items[i] != CtxItem::None; ++i)
    {
        const i32 ch = (g_ctx_items[i] == CtxItem::Sep1 || g_ctx_items[i] == CtxItem::Sep2)
                           ? static_cast<i32>(kCtxSepH)
                           : static_cast<i32>(kCtxItemH);
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
    for (u32 i = idx; i + 1 < g_win_count; ++i)
        g_windows[i] = g_windows[i + 1];
    g_windows[g_win_count - 1] = tmp;
    for (u32 i = 0; i < g_win_count; ++i)
        g_windows[i].focused = (i == g_win_count - 1);
}

void close_window(u32 idx)
{
    if (idx >= g_win_count)
        return;
    for (u32 i = idx; i + 1 < g_win_count; ++i)
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
    for (u32 i = 0; i < g_win_count; ++i)
        g_windows[i].focused = false;
}

Window* open_window(WindowKind kind, const char* title, i32 w, i32 h)
{
    if (g_win_count >= kMaxWindows)
        return nullptr;
    for (u32 i = 0; i < g_win_count; ++i)
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
        ++i;
    }
    nw.title[i] = 0;
    ++g_win_count;
    focus_window(g_win_count - 1);
    g_dirty_scene = true;
    return &g_windows[g_win_count - 1];
}

void create_terminal_window()
{
    if (g_win_count >= kMaxWindows)
        return;
    Window& t = g_windows[g_win_count];
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
        ++i;
    }
    t.title[i] = 0;

    const i32 cw = t.w;
    const i32 ch = t.h - static_cast<i32>(kTitleH);
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
    if (g_term_cursor >= g_term_cols * g_term_rows)
        g_term_cursor = 0;

    ++g_win_count;
    focus_window(g_win_count - 1);
    g_dirty_scene = true;
}

void launch_terminal()
{
    for (u32 i = 0; i < g_win_count; ++i)
    {
        if (g_windows[i].kind == WindowKind::Terminal)
        {
            g_windows[i].minimized = false;
            focus_window(i);
            g_dirty_scene = true;
            return;
        }
    }
    create_terminal_window();
}

void launch_shortcut(ShortcutKind kind)
{
    switch (kind)
    {
    case ShortcutKind::Explorer:
        log::write(log::Level::Info, "gfx", "launch: Explorer");
        open_window(WindowKind::Explorer, "Explorer", 640, 440);
        break;
    case ShortcutKind::Settings:
        log::write(log::Level::Info, "gfx", "launch: Settings");
        open_window(WindowKind::Settings, "Settings", 640, 440);
        break;
    case ShortcutKind::Terminal:
        log::write(log::Level::Info, "gfx", "launch: Terminal");
        launch_terminal();
        break;
    case ShortcutKind::GameLchr:
        log::write(log::Level::Info, "gfx", "launch: Game Launcher");
        open_window(WindowKind::Generic, "Game Launcher", 640, 480);
        break;
    case ShortcutKind::ImageVw:
        log::write(log::Level::Info, "gfx", "launch: Image Viewer");
        open_window(WindowKind::ImageViewer, "Image Viewer", 640, 480);
        break;
    case ShortcutKind::Bin:
        log::write(log::Level::Info, "gfx", "launch: Recycle Bin");
        open_window(WindowKind::Bin, "Recycle Bin", 620, 400);
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
    case MenuItem::GameLchr:
        launch_shortcut(ShortcutKind::GameLchr);
        break;
    case MenuItem::ImageVw:
        launch_shortcut(ShortcutKind::ImageVw);
        break;
    case MenuItem::Bin:
        launch_shortcut(ShortcutKind::Bin);
        break;
    case MenuItem::Shutdown:
        acpi::power_off();
        break;
    case MenuItem::Restart:
        acpi::restart();
        break;
    default:
        break;
    }
}

void ctx_action(CtxItem it)
{
    switch (it)
    {
    case CtxItem::New:
        log::write(log::Level::Info, "ctx", "New");
        break;
    case CtxItem::Refresh:
        log::write(log::Level::Info, "ctx", "Refresh");
        g_dirty_scene = true;
        break;
    case CtxItem::Cut:
        log::write(log::Level::Info, "ctx", "Cut");
        break;
    case CtxItem::Copy:
        log::write(log::Level::Info, "ctx", "Copy");
        break;
    case CtxItem::Paste:
        log::write(log::Level::Info, "ctx", "Paste");
        break;
    case CtxItem::Rename:
        log::write(log::Level::Info, "ctx", "Rename");
        break;
    case CtxItem::Properties:
        log::write(log::Level::Info, "ctx", "Properties");
        break;
    default:
        break;
    }
}

void open_context_menu(i32 x, i32 y)
{
    g_ctx_open = true;
    g_ctx_x = x;
    g_ctx_y = y;
    g_ctx_hover = -1;
    g_dirty_scene = true;
}

void close_context_menu()
{
    if (!g_ctx_open)
        return;
    g_ctx_open = false;
    g_ctx_hover = -1;
    g_dirty_scene = true;
}

void on_mouse_tick()
{
    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    const bool left = arch::x86_64::mouse_left();
    const bool right = arch::x86_64::mouse_right();

    const bool just_left_pressed = left && !g_prev_left;
    const bool just_left_released = !left && g_prev_left;
    const bool just_right_pressed = right && !g_prev_right;

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
    const i32 h_ctx = hit_ctx_item(mx, my);
    if (h_ctx != g_ctx_hover)
    {
        g_ctx_hover = h_ctx;
        g_dirty_scene = true;
    }

    if (just_right_pressed)
    {
        if (g_ctx_open)
            close_context_menu();
        else
        {
            const i32 widx = hit_window(mx, my);
            const i32 ty = to_i32(g_h) - static_cast<i32>(kTaskbarH);
            const bool over_taskbar = (my >= ty);
            const bool over_start = hit_start_button(mx, my);
            if (widx < 0 && !over_taskbar && !over_start)
                open_context_menu(mx, my);
        }
        g_prev_right = right;
        g_prev_mx = mx;
        g_prev_my = my;
        g_prev_left = left;
        return;
    }

    if (just_left_pressed && g_ctx_open)
    {
        if (h_ctx >= 0)
        {
            const CtxItem it = g_ctx_items[static_cast<u32>(h_ctx)];
            if (it != CtxItem::Sep1 && it != CtxItem::Sep2)
            {
                close_context_menu();
                ctx_action(it);
            }
        }
        else
            close_context_menu();
        g_prev_left = left;
        g_prev_right = right;
        g_prev_mx = mx;
        g_prev_my = my;
        return;
    }

    if (just_left_pressed)
    {
        if (hit_start_button(mx, my))
        {
            g_start_open = !g_start_open;
            g_hover_menu = -1;
            g_dirty_scene = true;
            g_prev_mx = mx;
            g_prev_my = my;
            g_prev_left = left;
            g_prev_right = right;
            return;
        }

        if (g_start_open)
        {
            if (h_menu >= 0 && h_menu < 7)
            {
                const MenuItem kinds[7] = {
                    MenuItem::Explorer, MenuItem::Settings, MenuItem::Terminal, MenuItem::GameLchr,
                    MenuItem::ImageVw,  MenuItem::Bin,      MenuItem::Shutdown};
                g_start_open = false;
                launch_menu_item(kinds[static_cast<u32>(h_menu)]);
                g_dirty_scene = true;
                g_prev_mx = mx;
                g_prev_my = my;
                g_prev_left = left;
                g_prev_right = right;
                return;
            }
            g_start_open = false;
            g_dirty_scene = true;
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
                if (w.kind == WindowKind::Explorer)
                    apps_click_explorer(mx, my, just_left_pressed);
                if (w.kind == WindowKind::Settings)
                    apps_click_settings(mx, my, just_left_pressed);
                if (w.kind == WindowKind::Bin)
                    apps_click_bin(mx, my, just_left_pressed);
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
            const i32 ty = to_i32(g_h) - static_cast<i32>(kTaskbarH);
            if (my >= ty && my < to_i32(g_h))
            {
                const i32 start_x = taskbar_cluster_x();
                i32 bx = start_x + 88;
                for (u32 i = 0; i < g_win_count; ++i)
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
                    bx += static_cast<i32>(tw) + 6;
                }
            }
            else
            {
                for (u32 i = 0; i < g_win_count; ++i)
                    g_windows[i].focused = false;
                g_focus_shortcut = -1;
                g_dirty_scene = true;
            }
        }
    }

    if (just_left_released)
        g_drag_win = -1;

    if (g_drag_win >= 0 && left)
    {
        Window& w = g_windows[static_cast<u32>(g_drag_win)];
        if (w.maximized)
            w.maximized = false;
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

    const i32 wheel = arch::x86_64::mouse_wheel();
    if (wheel != 0)
    {
        arch::x86_64::mouse_wheel_clear();
        const i32 idx = hit_window(mx, my);
        if (idx >= 0 && g_windows[static_cast<u32>(idx)].kind == WindowKind::Terminal)
        {
            Compositor::term_scroll_by(wheel * 3);
            g_dirty_scene = true;
        }
    }

    g_prev_mx = mx;
    g_prev_my = my;
    g_prev_left = left;
    g_prev_right = right;
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

void Compositor::machine_shutdown() noexcept
{
    acpi::power_off();
}
void Compositor::machine_restart() noexcept
{
    acpi::restart();
}

void Compositor::init() noexcept
{
    if (!fb::Framebuffer::ready())
        return;
    apply_theme();

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
            ++i;
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

    for (u32 i = 0; i < g_term_cols * g_term_rows; ++i)
        g_term[i] = 0;
    g_term_cursor = 0;
    g_term_scroll = 0;

    const auto dt = arch::x86_64::rtc::read();
    g_rtc_boot_unix = arch::x86_64::rtc::to_unix_seconds(dt);
    g_rtc_boot_ticks = arch::x86_64::pit_ticks();
    g_clock_sec = g_rtc_boot_unix;

    g_ready = true;
    g_dirty_scene = true;

    scene_render();
    fb_blit_full();

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();
    g_cursor_x = -1000;
    cursor_draw(mx, my);

    apps_bind_impl(widget_rect_cb, widget_text_cb);

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
        gfx::hal_set_target(g_scene, g_w, g_h, g_w);
        gpu::bind_surface(g_scene, g_w, g_h, g_w);
        gfx::Device::begin_frame();
        scene_render();
        gfx::Device::end_frame();
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
            g_term_scroll += static_cast<u32>(delta);
        else
            g_term_scroll = g_term_rows - 1;
    }
    else if (delta < 0)
    {
        const u32 mag = static_cast<u32>(-delta);
        g_term_scroll = (mag >= g_term_scroll) ? 0 : (g_term_scroll - mag);
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
    for (u32 i = 0; i < g_term_cols * g_term_rows; ++i)
        g_term[i] = 0;
    g_term_cursor = 0;
    g_term_scroll = 0;
    g_dirty_scene = true;
}

void Compositor::term_put(char c) noexcept
{
    if (!g_ready)
        return;
    if (c == '\f')
    {
        term_clear();
        return;
    }
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
        g_term[g_term_cursor++] = c;

    if (g_term_cursor >= g_term_cols * g_term_rows)
    {
        libk::memmove(g_term, g_term + g_term_cols, (g_term_rows - 1) * g_term_cols);
        for (u32 i = 0; i < g_term_cols; ++i)
            g_term[(g_term_rows - 1) * g_term_cols + i] = 0;
        g_term_cursor = (g_term_rows - 1) * g_term_cols;
    }
    g_term_scroll = 0;
    g_dirty_scene = true;
}

void Compositor::update_clock() noexcept
{
    const u64 now = g_rtc_boot_unix + (arch::x86_64::pit_ticks() - g_rtc_boot_ticks) / 100u;
    if (now == g_clock_sec)
        return;
    g_clock_sec = now;
    g_dirty_scene = true;
}

void Compositor::set_theme(theme::Id id) noexcept
{
    theme::set(id);
    apply_theme();
    g_dirty_scene = true;
}

} // namespace notyvos::gfx
