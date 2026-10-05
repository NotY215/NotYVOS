#include <kernel/acpi/acpi.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/gfx/apps.hpp>
#include <kernel/gfx/widget.hpp>
#include <kernel/img/decoder.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/img/decoder.hpp>
#include <kernel/gfx/compositor.hpp>
#include <kernel/gfx/theme.hpp>
#include <kernel/fb/framebuffer.hpp>

namespace notyvos::gfx
{

namespace
{

void (*g_rect)(i32, i32, i32, i32, u32) = nullptr;
void (*g_text)(i32, i32, const char*, u32, u32) = nullptr;

void r(i32 x, i32 y, i32 w, i32 h, u32 c)
{
    if (g_rect)
        g_rect(x, y, w, h, c);
}
void t(i32 x, i32 y, const char* s, u32 fg, u32 bg)
{
    if (g_text)
        g_text(x, y, s, fg, bg);
}

constexpr u32 kCellW = 16;

constexpr u32 kMenuBg = 0x00F0F0F0;
constexpr u32 kMenuFg = 0x00202020;
constexpr u32 kToolBg = 0x00E8E8E8;
constexpr u32 kToolEdge = 0x00C0C0C0;
constexpr u32 kToolHi = 0x00D0E4F4;
constexpr u32 kAddressBg = 0x00FFFFFF;
constexpr u32 kAddressEdge = 0x00A0A0A0;
constexpr u32 kNavBg = 0x00E8E8E8;
constexpr u32 kNavFg = 0x00202020;
constexpr u32 kNavHi = 0x00CCE4FC;
constexpr u32 kContentBg = 0x00FFFFFF;
constexpr u32 kContentFg = 0x00202020;
constexpr u32 kContentHi = 0x00CCE4FC;
constexpr u32 kHeaderBg = 0x00F0F0F0;
constexpr u32 kHeaderFg = 0x00404040;
constexpr u32 kHeaderEdge = 0x00C0C0C0;
constexpr u32 kStatusBg = 0x00F0F0F0;
constexpr u32 kStatusFg = 0x00303030;
constexpr u32 kIcon = 0x00FFB060;

void icon_folder(i32 x, i32 y, i32 s)
{
    r(x + 1, y + 4, s - 6, s - 8, kIcon);
    r(x + 1, y + 7, s - 2, s - 9, kIcon);
    r(x + 1, y + 7, s - 2, 1, 0x00906020);
}
void icon_file(i32 x, i32 y, i32 s)
{
    r(x + 3, y + 1, s - 6, s - 2, 0x00FFFFFF);
    r(x + 3, y + 1, s - 6, 1, 0x00A0A0A0);
    r(x + 3, y + s - 2, s - 6, 1, 0x00A0A0A0);
    r(x + 3, y + 1, 1, s - 2, 0x00A0A0A0);
    r(x + s - 4, y + 1, 1, s - 2, 0x00A0A0A0);
    r(x + 5, y + 5, s - 10, 1, 0x00C0C0C0);
    r(x + 5, y + 7, s - 10, 1, 0x00C0C0C0);
    r(x + 5, y + 9, s - 10, 1, 0x00C0C0C0);
}

void arrow_left(i32 x, i32 y)
{
    for (i32 i = 0; i < 7; ++i)
        r(x + 5 - i, y + 4 + i, 1, 8 - i * 2, 0x00404040);
}
void arrow_right(i32 x, i32 y)
{
    for (i32 i = 0; i < 7; ++i)
        r(x + i, y + 4 + i, 1, 8 - i * 2, 0x00404040);
}
void arrow_up(i32 x, i32 y)
{
    for (i32 i = 0; i < 7; ++i)
        r(x + 1 + i, y + 1 + i, 14 - i * 2, 1, 0x00404040);
}
void arrow_refresh(i32 x, i32 y)
{
    for (i32 i = 0; i < 12; ++i)
    {
        r(x + 2 + i, y + 1, 1, 2, 0x00305090);
        r(x + 2 + i, y + 11, 1, 2, 0x00305090);
        r(x + 1, y + 2 + i, 2, 1, 0x00305090);
        r(x + 11, y + 2 + i, 2, 1, 0x00305090);
    }
}

// ---------------------------------------------------------------------------
// Explorer
// ---------------------------------------------------------------------------

constexpr u32 kMaxEntries = 32;

struct ExplEntry
{
    char name[64];
    bool is_dir;
    u32 size;
};

// Forward declaration — the definition sits after draw_explorer().
void draw_explorer_context_menu();

// Navigation history: 32 entries max, like a small browser history.
constexpr u32 kMaxHistory = 32;

struct ExplorerState
{
    bool initialized;
    ExplEntry entries[kMaxEntries];
    u32 entry_count;
    i32 sel;
    i32 hover;
    i32 nav_hover;
    char address[128];

    // Current directory path, used to build child paths on double-click.
    char cwd[128];

    // Navigation history.
    char history[kMaxHistory][128];
    u32 history_count;
    u32 history_pos; // index of the current entry
    i32 last_click;  // entry index of the previous click (for dbl-click)
    u64 last_click_tick;

    // Context menu (right-click inside the window).
    bool ctx_open;
    i32 ctx_x;
    i32 ctx_y;
    i32 ctx_hover;
    i32 ctx_target;
};
ExplorerState g_exp{};

// Rescan the current directory. The VFS is a simple tree, so we walk it
// each time. Real per-directory caching lands when the VFS grows an
// inode/dentry cache.
void explorer_refresh();
void explorer_navigate_to(const char* abs_path);
void explorer_go_back();
void explorer_go_forward();
void explorer_go_up();

enum class ExpCtxItem : u8
{
    None = 0,
    Open,
    Sep1,
    NewFolder,
    Refresh,
    Sep2,
    Rename,
    Delete,
    Sep3,
    Properties,
};

const ExpCtxItem g_exp_ctx[] = {ExpCtxItem::Open,    ExpCtxItem::Sep1, ExpCtxItem::NewFolder,
                                ExpCtxItem::Refresh, ExpCtxItem::Sep2, ExpCtxItem::Rename,
                                ExpCtxItem::Delete,  ExpCtxItem::Sep3, ExpCtxItem::Properties,
                                ExpCtxItem::None};

const char* exp_ctx_label(ExpCtxItem it)
{
    switch (it)
    {
    case ExpCtxItem::Open:
        return "Open";
    case ExpCtxItem::NewFolder:
        return "New folder";
    case ExpCtxItem::Refresh:
        return "Refresh";
    case ExpCtxItem::Rename:
        return "Rename";
    case ExpCtxItem::Delete:
        return "Delete";
    case ExpCtxItem::Properties:
        return "Properties";
    default:
        return "";
    }
}

i32 exp_ctx_height()
{
    i32 h = 0;
    for (u32 i = 0; g_exp_ctx[i] != ExpCtxItem::None; ++i)
        h += (g_exp_ctx[i] == ExpCtxItem::Sep1 || g_exp_ctx[i] == ExpCtxItem::Sep2 ||
              g_exp_ctx[i] == ExpCtxItem::Sep3)
                 ? 6
                 : 24;
    return h;
}

void explorer_collect()
{
    g_exp.entry_count = 0;

    // Resolve the current directory.
    auto* dir = fs::vfs_lookup(g_exp.cwd, "/");
    if (!dir)
        dir = fs::vfs_root();
    if (!dir)
        return;

    // ".." entry (except at root).
    if (libk::strcmp(g_exp.cwd, "/") != 0)
    {
        ExplEntry& e = g_exp.entries[g_exp.entry_count];
        e.name[0] = '.';
        e.name[1] = '.';
        e.name[2] = 0;
        e.is_dir = true;
        e.size = 0;
        ++g_exp.entry_count;
    }

    for (fs::VNode* c = dir->children; c && g_exp.entry_count < kMaxEntries; c = c->next)
    {
        ExplEntry& e = g_exp.entries[g_exp.entry_count];
        u32 i = 0;
        while (c->name[i] && i < 63)
        {
            e.name[i] = c->name[i];
            ++i;
        }
        e.name[i] = 0;
        e.is_dir = (c->type == fs::VType::Dir);
        e.size = 0;
        if (c->ops && c->ops->size)
        {
            const isize s = c->ops->size(c);
            if (s > 0)
                e.size = static_cast<u32>(s);
        }
        ++g_exp.entry_count;
    }
}

void explorer_refresh()
{
    explorer_collect();
    g_exp.sel = -1;
    g_exp.hover = -1;
}

void explorer_navigate_to(const char* abs_path)
{
    if (!abs_path)
        return;

    // Push the current path onto history, if it differs.
    if (g_exp.history_count > 0 && g_exp.history_pos < g_exp.history_count)
    {
        if (libk::strcmp(g_exp.history[g_exp.history_pos], abs_path) == 0)
            return; // no-op
    }

    // Truncate forward history.
    g_exp.history_count = (g_exp.history_pos + 1u > g_exp.history_count) ? g_exp.history_count
                                                                         : g_exp.history_pos + 1u;

    if (g_exp.history_count >= kMaxHistory)
    {
        // Shift left by one to make room.
        for (u32 i = 1; i < kMaxHistory; ++i)
            libk::strcpy(g_exp.history[i - 1], g_exp.history[i]);
        --g_exp.history_count;
    }

    u32 i = 0;
    while (abs_path[i] && i < 127)
    {
        g_exp.history[g_exp.history_count][i] = abs_path[i];
        ++i;
    }
    g_exp.history[g_exp.history_count][i] = 0;
    g_exp.history_pos = g_exp.history_count;
    ++g_exp.history_count;

    // Update cwd and address.
    i = 0;
    while (abs_path[i] && i < 127)
    {
        g_exp.cwd[i] = abs_path[i];
        ++i;
    }
    g_exp.cwd[i] = 0;
    libk::strcpy(g_exp.address, g_exp.cwd);

    explorer_refresh();
}

void explorer_go_back()
{
    if (g_exp.history_pos == 0)
        return;
    --g_exp.history_pos;
    libk::strcpy(g_exp.cwd, g_exp.history[g_exp.history_pos]);
    libk::strcpy(g_exp.address, g_exp.cwd);
    explorer_refresh();
}

void explorer_go_forward()
{
    if (g_exp.history_pos + 1 >= g_exp.history_count)
        return;
    ++g_exp.history_pos;
    libk::strcpy(g_exp.cwd, g_exp.history[g_exp.history_pos]);
    libk::strcpy(g_exp.address, g_exp.cwd);
    explorer_refresh();
}

void explorer_go_up()
{
    if (libk::strcmp(g_exp.cwd, "/") == 0)
        return;

    // Strip the last component.
    char parent[128];
    u32 i = 0;
    while (g_exp.cwd[i] && i < 127)
    {
        parent[i] = g_exp.cwd[i];
        ++i;
    }
    parent[i] = 0;

    // Walk back to the last '/'.
    while (i > 1 && parent[i - 1] != '/')
        --i;
    if (i > 1)
        --i;
    parent[i] = 0;
    if (parent[0] == 0)
    {
        parent[0] = '/';
        parent[1] = 0;
    }

    explorer_navigate_to(parent);
}

void explorer_init()
{
    if (g_exp.initialized)
        return;
    g_exp.cwd[0] = '/';
    g_exp.cwd[1] = 0;
    g_exp.sel = -1;
    g_exp.hover = -1;
    g_exp.nav_hover = 0;
    g_exp.history_count = 0;
    g_exp.history_pos = 0;
    g_exp.last_click = -1;
    g_exp.last_click_tick = 0;
    libk::strcpy(g_exp.address, "/");
    explorer_refresh();
    g_exp.initialized = true;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

enum class SettingsTab : u8
{
    System,
    Display,
    Storage,
    Input,
    Network,
    Appearance,
    About
};

struct SettingsState
{
    bool initialized;
    SettingsTab current;
    i32 hover_tab;
    i32 theme_hover; // -1 = none, else index into theme::count()
};
SettingsState g_set{};

const char* tab_label(SettingsTab t)
{
    switch (t)
    {
    case SettingsTab::System:
        return "System";
    case SettingsTab::Display:
        return "Display";
    case SettingsTab::Storage:
        return "Storage";
    case SettingsTab::Input:
        return "Input";
    case SettingsTab::Network:
        return "Network";
    case SettingsTab::Appearance:
        return "Appearance";
    case SettingsTab::About:
        return "About";
    }
    return "";
}

// ---------------------------------------------------------------------------
// Bin
// ---------------------------------------------------------------------------

struct BinState
{
    bool initialized;
};
BinState g_bin{};

// ---------------------------------------------------------------------------
// Image Viewer
// ---------------------------------------------------------------------------

struct ImgViewerState
{
    bool initialized;
    img::Image image;
    char name[64];
    i32 pan_x;
    i32 pan_y;
};
ImgViewerState g_img{};

void draw_menu_bar(i32 gx, i32 gy, i32 gw)
{
    const i32 h = 22;
    r(gx, gy, gw, h, kMenuBg);
    r(gx, gy + h - 1, gw, 1, kHeaderEdge);
    const char* items[4] = {"File", "Edit", "View", "Help"};
    i32 x = gx + 8;
    for (const char* s : items)
    {
        u32 len = 0;
        while (s[len])
            ++len;
        t(x, gy + 3, s, kMenuFg, kMenuBg);
        x += static_cast<i32>(len) * static_cast<i32>(kCellW) + 20;
    }
}

void draw_toolbar(i32 gx, i32 gy, i32 gw)
{
    const i32 h = 30;
    r(gx, gy, gw, h, kToolBg);
    r(gx, gy + h - 1, gw, 1, kToolEdge);
    // Back: dim when no history.
    const bool can_back = (g_exp.history_pos > 0);
    if (g_exp.nav_hover == 1 && can_back)
        r(gx + 4, gy + 4, 28, 22, kToolHi);
    arrow_left(gx + 10, gy + 8);
    if (!can_back)
        r(gx + 4, gy + 4, 28, 22, 0x00E8E8E8);

    // Forward: dim when at the tip of history.
    const bool can_fwd = (g_exp.history_pos + 1 < g_exp.history_count);
    if (g_exp.nav_hover == 2 && can_fwd)
        r(gx + 36, gy + 4, 28, 22, kToolHi);
    arrow_right(gx + 42, gy + 8);
    if (!can_fwd)
        r(gx + 36, gy + 4, 28, 22, 0x00E8E8E8);

    // Up: dim at root.
    const bool can_up = (libk::strcmp(g_exp.cwd, "/") != 0);
    if (g_exp.nav_hover == 3 && can_up)
        r(gx + 68, gy + 4, 28, 22, kToolHi);
    arrow_up(gx + 74, gy + 8);
    if (!can_up)
        r(gx + 68, gy + 4, 28, 22, 0x00E8E8E8);
    if (g_exp.nav_hover == 4)
        r(gx + 100, gy + 4, 28, 22, kToolHi);
    arrow_refresh(gx + 106, gy + 8);

    const i32 ax = gx + 140;
    const i32 aw = gw - (ax - gx) - 8;
    r(ax, gy + 5, aw, 20, kAddressBg);
    r(ax, gy + 5, aw, 1, kAddressEdge);
    r(ax, gy + 24, aw, 1, kAddressEdge);
    r(ax, gy + 5, 1, 20, kAddressEdge);
    r(ax + aw - 1, gy + 5, 1, 20, kAddressEdge);
    t(ax + 6, gy + 7, g_exp.address, kMenuFg, kAddressBg);
}

void draw_navigation_pane(i32 gx, i32 gy, i32, i32 gh)
{
    const i32 w = 160;
    r(gx, gy, w, gh, kNavBg);
    r(gx + w - 1, gy, 1, gh, kHeaderEdge);
    t(gx + 8, gy + 8, "Quick access", kNavFg, kNavBg);
    r(gx + 8, gy + 28, w - 16, 1, kHeaderEdge);
    const char* items[4] = {"Desktop", "Downloads", "Documents", "This PC"};
    i32 y = gy + 38;
    for (const char* s : items)
    {
        t(gx + 24, y, s, kNavFg, kNavBg);
        icon_folder(gx + 6, y, 14);
        y += 24;
    }
}

void draw_explorer_status(i32 gx, i32 gy, i32 gw)
{
    const i32 h = 22;
    r(gx, gy, gw, h, kStatusBg);
    r(gx, gy, gw, 1, kHeaderEdge);
    char buf[32];
    int n = 0;
    u32 c = g_exp.entry_count;
    if (c == 0)
        buf[n++] = '0';
    while (c)
    {
        buf[n++] = static_cast<char>('0' + (c % 10));
        c /= 10;
    }
    for (int i = 0; i < n / 2; ++i)
    {
        char tmp = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = tmp;
    }
    buf[n++] = ' ';
    buf[n++] = 'i';
    buf[n++] = 't';
    buf[n++] = 'e';
    buf[n++] = 'm';
    buf[n++] = 's';
    buf[n] = 0;
    t(gx + 8, gy + 3, buf, kStatusFg, kStatusBg);
}

void draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool)
{
    explorer_init();
    draw_menu_bar(gx, gy, gw);
    gy += 22;
    gh -= 22;
    draw_toolbar(gx, gy, gw);
    gy += 30;
    gh -= 30;

    const i32 nav_w = 160;
    draw_navigation_pane(gx, gy, gw, gh);

    const i32 cx = gx + nav_w;
    const i32 cw = gw - nav_w;
    r(cx, gy, cw, gh - 22, kContentBg);

    const i32 header_h = 22;
    r(cx, gy, cw, header_h, kHeaderBg);
    r(cx, gy + header_h - 1, cw, 1, kHeaderEdge);
    t(cx + 40, gy + 3, "Name", kHeaderFg, kHeaderBg);
    t(cx + cw - 200, gy + 3, "Type", kHeaderFg, kHeaderBg);
    t(cx + cw - 90, gy + 3, "Size", kHeaderFg, kHeaderBg);

    const i32 row_h = 22;
    i32 ry = gy + header_h + 2;
    g_exp.hover = -1;
    for (u32 i = 0; i < g_exp.entry_count; ++i)
    {
        if (ry + row_h > gy + gh - 22)
            break;
        const bool hover = (mx >= cx && mx < cx + cw && my >= ry && my < ry + row_h);
        if (hover)
            g_exp.hover = static_cast<i32>(i);

        const u32 bg = (static_cast<i32>(i) == g_exp.sel) ? kContentHi
                       : hover                            ? kContentHi
                                                          : kContentBg;
        if (bg != kContentBg)
            r(cx + 2, ry, cw - 4, row_h - 1, bg);

        const ExplEntry& e = g_exp.entries[i];
        if (e.is_dir)
            icon_folder(cx + 20, ry + 3, 16);
        else
            icon_file(cx + 20, ry + 3, 16);
        t(cx + 40, ry + 3, e.name, kContentFg, bg);
        t(cx + cw - 200, ry + 3, e.is_dir ? "Folder" : "File", kContentFg, bg);
        if (!e.is_dir)
        {
            char sb[16];
            int n = 0;
            u32 v = e.size;
            if (v == 0)
                sb[n++] = '0';
            while (v)
            {
                sb[n++] = static_cast<char>('0' + v % 10);
                v /= 10;
            }
            sb[n++] = ' ';
            sb[n++] = 'B';
            for (int k = 0; k < n / 2; ++k)
            {
                char tmp = sb[k];
                sb[k] = sb[n - 1 - k];
                sb[n - 1 - k] = tmp;
            }
            sb[n] = 0;
            t(cx + cw - 90, ry + 3, sb, kContentFg, bg);
        }
        ry += row_h;
    }
    draw_explorer_status(gx, gy + gh - 22, gw);
    draw_explorer_context_menu();
}

void draw_explorer_context_menu()
{
    if (!g_exp.ctx_open)
        return;
    const i32 w = 180;
    const i32 h = exp_ctx_height();

    const i32 screen_w = static_cast<i32>(fb::Framebuffer::width());
    const i32 screen_h = static_cast<i32>(fb::Framebuffer::height());

    i32 x = g_exp.ctx_x;
    i32 y = g_exp.ctx_y;
    if (x + w > screen_w)
        x = screen_w - w;
    if (y + h > screen_h - 30)
        y = screen_h - 30 - h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    g_exp.ctx_x = x;
    g_exp.ctx_y = y;

    r(x, y, w, h, 0x00F8F8F8);
    r(x, y, w, 1, 0x00A0A0A0);
    r(x, y + h - 1, w, 1, 0x00A0A0A0);
    r(x, y, 1, h, 0x00A0A0A0);
    r(x + w - 1, y, 1, h, 0x00A0A0A0);

    i32 cy = y;
    for (u32 i = 0; g_exp_ctx[i] != ExpCtxItem::None; ++i)
    {
        const ExpCtxItem it = g_exp_ctx[i];
        if (it == ExpCtxItem::Sep1 || it == ExpCtxItem::Sep2 || it == ExpCtxItem::Sep3)
        {
            r(x + 8, cy + 2, w - 16, 1, 0x00D0D0D0);
            cy += 6;
            continue;
        }
        const bool hover = (static_cast<i32>(i) == g_exp.ctx_hover);
        const u32 bg = hover ? 0x00CCE4FC : 0x00F8F8F8;
        if (hover)
            r(x + 2, cy + 1, w - 4, 22, bg);
        t(x + 16, cy + 4, exp_ctx_label(it), 0x00202020, bg);
        cy += 24;
    }
}

void draw_theme_swatch(const theme::Palette& p, i32 x, i32 y, i32 w, i32 h)
{
    const i32 grad_h = (h * 7) / 10;
    for (i32 i = 0; i < grad_h; ++i)
    {
        const u32 t = (static_cast<u32>(i) * 32u) / static_cast<u32>(grad_h ? grad_h : 1);
        const u32 rr = ((p.desktop_top >> 16) & 0xFFu) * (32u - t) / 32u +
                       ((p.desktop_bottom >> 16) & 0xFFu) * t / 32u;
        const u32 gg = ((p.desktop_top >> 8) & 0xFFu) * (32u - t) / 32u +
                       ((p.desktop_bottom >> 8) & 0xFFu) * t / 32u;
        const u32 bb =
            (p.desktop_top & 0xFFu) * (32u - t) / 32u + (p.desktop_bottom & 0xFFu) * t / 32u;
        r(x, y + i, w, 1, (rr << 16) | (gg << 8) | bb);
    }
    r(x, y + grad_h, w, h - grad_h, p.taskbar_bg);
    r(x, y + grad_h, w / 3, h - grad_h, p.accent);
    r(x, y, w, 1, p.border_unfocused);
    r(x, y + h - 1, w, 1, p.border_unfocused);
    r(x, y, 1, h, p.border_unfocused);
    r(x + w - 1, y, 1, h, p.border_unfocused);
}

void draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool)
{
    if (!g_set.initialized)
    {
        g_set.current = SettingsTab::System;
        g_set.hover_tab = -1;
        g_set.theme_hover = -1;
        g_set.initialized = true;
    }

    const i32 side_w = 200;
    r(gx, gy, side_w, gh, 0x00F0F0F0);
    r(gx + side_w - 1, gy, 1, gh, kHeaderEdge);
    r(gx, gy, side_w, 40, 0x00E4E4E4);
    t(gx + 16, gy + 14, "Settings", 0x00202020, 0x00E4E4E4);

    const SettingsTab tabs[7] = {SettingsTab::System, SettingsTab::Display, SettingsTab::Storage,
                                 SettingsTab::Input,  SettingsTab::Network, SettingsTab::Appearance,
                                 SettingsTab::About};
    i32 ty = gy + 52;
    g_set.hover_tab = -1;
    for (u32 i = 0; i < 7; ++i)
    {
        const bool hover = (mx >= gx && mx < gx + side_w - 1 && my >= ty && my < ty + 34);
        if (hover)
            g_set.hover_tab = static_cast<i32>(i);
        const u32 bg = (tabs[i] == g_set.current) ? kNavHi : hover ? 0x00DCE4EC : 0x00F0F0F0;
        if (bg != 0x00F0F0F0)
            r(gx + 4, ty, side_w - 8, 32, bg);
        t(gx + 16, ty + 9, tab_label(tabs[i]), 0x00202020, bg);
        ty += 36;
    }

    const i32 rx = gx + side_w + 8;
    const i32 rw = gw - side_w - 16;
    r(rx, gy, rw, gh, kContentBg);
    t(rx + 16, gy + 16, tab_label(g_set.current), 0x00202020, kContentBg);
    r(rx + 16, gy + 40, rw - 32, 1, kHeaderEdge);

    i32 cy = gy + 56;
    auto label_row = [&](const char* key, const char* value)
    {
        t(rx + 16, cy, key, 0x00404040, kContentBg);
        t(rx + 240, cy, value, 0x00202020, kContentBg);
        cy += 22;
    };

    switch (g_set.current)
    {
    case SettingsTab::System:
        label_row("Operating System", "NOTYVOS 0.1.0");
        label_row("Build", "phase 6E");
        label_row("Kernel", "x86-64, C++20");
        label_row("Heap", "64 MiB");
        label_row("Scheduler", "round-robin");
        break;
    case SettingsTab::Display:
        label_row("Resolution", "800 x 600");
        label_row("Colour depth", "32-bit ARGB");
        label_row("Backend", "software + VBE");
        label_row("Wallpaper", "wallpaper.raw");
        break;
    case SettingsTab::Storage:
        label_row("Device", "sda (AHCI)");
        label_row("Sectors", "524288");
        label_row("Filesystem", "NYFS");
        label_row("Mount point", "/disk");
        break;
    case SettingsTab::Input:
        label_row("Keyboard", "PS/2 i8042, IRQ1");
        label_row("Mouse", "PS/2, IRQ12, wheel");
        label_row("Fallback input", "COM1 serial");
        break;
    case SettingsTab::Network:
        label_row("e1000 driver", "not detected");
        label_row("Wi-Fi", "not yet implemented");
        label_row("Firewall", "not yet implemented");
        break;
    case SettingsTab::Appearance:
    {
        t(rx + 16, cy, "Theme", 0x00404040, kContentBg);
        cy += 26;

        const theme::Id active = theme::current_id();
        g_set.theme_hover = -1;

        const u32 theme_count = theme::count();
        for (u32 i = 0; i < theme_count; ++i)
        {
            const theme::Id id = static_cast<theme::Id>(i);
            const theme::Palette& p = theme::get(id);

            const i32 row_x = rx + 16;
            const i32 row_w = rw - 32;
            const i32 row_h = 68;
            const bool hover = (mx >= row_x && mx < row_x + row_w && my >= cy && my < cy + row_h);
            if (hover)
                g_set.theme_hover = static_cast<i32>(i);

            const bool current = (id == active);
            const u32 row_bg = hover ? 0x00E8F0F8 : 0x00F4F6F8;
            r(row_x, cy, row_w, row_h, row_bg);
            r(row_x, cy, row_w, 1, kHeaderEdge);
            r(row_x, cy + row_h - 1, row_w, 1, kHeaderEdge);
            r(row_x, cy, 1, row_h, kHeaderEdge);
            r(row_x + row_w - 1, cy, 1, row_h, kHeaderEdge);
            if (current)
                r(row_x, cy, 3, row_h, 0x0060A0E8);

            // Theme name and swatch.
            t(row_x + 16, cy + 12, p.name, 0x00202020, row_bg);
            t(row_x + 16, cy + 34,
              (id == theme::Id::Dark)    ? "Deep greys, high contrast."
              : (id == theme::Id::Light) ? "Bright surfaces, low blue."
                                         : "macOS-inspired accent.",
              0x00606070, row_bg);

            draw_theme_swatch(p, row_x + row_w - 130, cy + 10, 110, 48);

            if (current)
                t(row_x + row_w - 190, cy + 26, "Active", 0x003070C0, row_bg);

            cy += row_h + 8;
        }

        cy += 8;
        t(rx + 16, cy, "Changes apply immediately.", 0x00606070, kContentBg);
        break;
    }
    case SettingsTab::About:
    {
        t(rx + 16, cy, "NOTYVOS", 0x00202020, kContentBg);
        cy += 22;
        t(rx + 16, cy, "Lightweight x86-64 operating system.", 0x00404040, kContentBg);
        cy += 20;
        t(rx + 16, cy, "License: AGPL-3.0-or-later", 0x00404040, kContentBg);
        cy += 30;

        static Button s_shutdown;
        static Button s_restart;
        s_shutdown.init(
            "Shut down", {rx + 16, cy, 140, 32}, [](void*) { acpi::power_off(); }, nullptr);
        s_restart.init("Restart", {rx + 170, cy, 140, 32}, [](void*) { acpi::restart(); }, nullptr);
        s_shutdown.draw(mx, my, false);
        s_restart.draw(mx, my, false);
        break;
    }
    }
}

void draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32, i32, bool)
{
    if (!g_bin.initialized)
        g_bin.initialized = true;
    const i32 header_h = 24;
    r(gx, gy, gw, header_h, kHeaderBg);
    r(gx, gy + header_h - 1, gw, 1, kHeaderEdge);
    t(gx + 40, gy + 4, "Name", kHeaderFg, kHeaderBg);
    t(gx + gw - 220, gy + 4, "Original", kHeaderFg, kHeaderBg);
    t(gx + gw - 100, gy + 4, "Date deleted", kHeaderFg, kHeaderBg);
    r(gx, gy + header_h, gw, gh - header_h - 22, kContentBg);
    t(gx + 20, gy + header_h + 20, "This folder is empty.", 0x00606060, kContentBg);
    r(gx, gy + gh - 22, gw, 22, kStatusBg);
    r(gx, gy + gh - 22, gw, 1, kHeaderEdge);
    t(gx + 8, gy + gh - 19, "0 items", kStatusFg, kStatusBg);
}

// --- Image Viewer ---------------------------------------------------------

void imgviewer_load_path(const char* path) noexcept
{
    img::free(g_img.image);
    g_img.pan_x = 0;
    g_img.pan_y = 0;
    g_img.name[0] = 0;
    if (!path)
        return;

    auto* vn = fs::vfs_lookup(path, "/");
    if (!vn || !vn->ops || !vn->ops->size || !vn->ops->read)
        return;

    const isize sz = vn->ops->size(vn);
    if (sz <= 0 || sz > 32 * 1024 * 1024)
        return;

    auto* buf = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz)));
    if (!buf)
        return;

    isize got = 0;
    while (got < sz)
    {
        const isize n =
            vn->ops->read(vn, buf + got, static_cast<usize>(got), static_cast<usize>(sz - got));
        if (n <= 0)
            break;
        got += n;
    }
    if (got == sz)
    {
        (void)img::decode(buf, static_cast<usize>(sz), g_img.image);
        u32 i = 0;
        while (path[i] && i < 63)
        {
            g_img.name[i] = path[i];
            ++i;
        }
        g_img.name[i] = 0;
    }
    mm::Heap::deallocate(buf);
}

void draw_imageviewer(i32 gx, i32 gy, i32 gw, i32 gh, i32, i32, bool)
{
    if (!g_img.initialized)
    {
        g_img.initialized = true;
        g_img.pan_x = 0;
        g_img.pan_y = 0;
        g_img.name[0] = 0;
        // Auto-load a demo file if present.
        imgviewer_load_path("/wallpaper.bmp");
    }

    const i32 bar_h = 22;
    r(gx, gy, gw, bar_h, kHeaderBg);
    r(gx, gy + bar_h - 1, gw, 1, kHeaderEdge);
    if (g_img.name[0] != 0)
        t(gx + 8, gy + 4, g_img.name, kHeaderFg, kHeaderBg);
    else
        t(gx + 8, gy + 4, "No image loaded", kHeaderFg, kHeaderBg);

    r(gx, gy + bar_h, gw, gh - bar_h, 0x00202028);

    if (g_img.image.pixels && g_img.image.width > 0 && g_img.image.height > 0)
    {
        const i32 avail_w = gw - 20;
        const i32 avail_h = gh - bar_h - 20;
        i32 scale = 1;
        if (static_cast<i32>(g_img.image.width) > avail_w ||
            static_cast<i32>(g_img.image.height) > avail_h)
        {
            const i32 sx = avail_w / static_cast<i32>(g_img.image.width);
            const i32 sy = avail_h / static_cast<i32>(g_img.image.height);
            scale = (sx < sy) ? sx : sy;
            if (scale < 1)
                scale = 1;
        }
        const i32 dw = static_cast<i32>(g_img.image.width) * scale;
        const i32 dh = static_cast<i32>(g_img.image.height) * scale;
        const i32 ox = gx + (gw - dw) / 2 + g_img.pan_x;
        const i32 oy = gy + bar_h + (gh - bar_h - dh) / 2 + g_img.pan_y;

        for (i32 y = 0; y < dh; ++y)
        {
            const u32 sy = static_cast<u32>(y) / static_cast<u32>(scale);
            for (i32 x = 0; x < dw; ++x)
            {
                const u32 sx = static_cast<u32>(x) / static_cast<u32>(scale);
                const u32 c = g_img.image.pixels[sy * g_img.image.width + sx];
                r(ox + x, oy + y, 1, 1, c);
            }
        }
    }
    else
    {
        t(gx + 20, gy + bar_h + 20, "(no image)", 0x00808080, 0x00202028);
        t(gx + 20, gy + bar_h + 44, "The Image Viewer loads BMP files.", 0x00808080, 0x00202028);
        t(gx + 20, gy + bar_h + 66, "PNG, JPEG, GIF, ICO recognisers are stubs.", 0x00808080,
          0x00202028);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Game Launcher
// ---------------------------------------------------------------------------

struct GameEntry
{
    char name[64];
    char path[128];
    u32 size;
};

struct GameLauncherState
{
    bool initialized;
    GameEntry games[32];
    u32 game_count;
    i32 sel;
    i32 hover;
    i32 tab; // 0 = library, 1 = settings
};
GameLauncherState g_gl{};

void gamelauncher_scan()
{
    g_gl.game_count = 0;

    // Look under /disk/games and /games.
    const char* dirs[2] = {"/disk/games", "/games"};
    for (const char* d : dirs)
    {
        auto* dir = fs::vfs_lookup(d, "/");
        if (!dir || dir->type != fs::VType::Dir)
            continue;

        for (fs::VNode* c = dir->children; c && g_gl.game_count < 32; c = c->next)
        {
            if (c->type != fs::VType::File)
                continue;
            const u32 len = static_cast<u32>(libk::strlen(c->name));
            if (len < 4)
                continue;
            // Accept .elf and .bin
            const char* ext = c->name + len - 4;
            const bool is_elf = libk::strcmp(ext, ".elf") == 0;
            const bool is_bin = libk::strcmp(ext, ".bin") == 0;
            if (!is_elf && !is_bin)
                continue;

            GameEntry& ge = g_gl.games[g_gl.game_count];
            u32 i = 0;
            while (c->name[i] && i < 63)
            {
                ge.name[i] = c->name[i];
                ++i;
            }
            ge.name[i] = 0;

            u32 k = 0;
            while (d[k] && k < 100)
            {
                ge.path[k] = d[k];
                ++k;
            }
            ge.path[k++] = '/';
            i = 0;
            while (c->name[i] && k < 127)
            {
                ge.path[k++] = c->name[i++];
            }
            ge.path[k] = 0;

            ge.size = 0;
            if (c->ops && c->ops->size)
            {
                const isize s = c->ops->size(c);
                if (s > 0)
                    ge.size = static_cast<u32>(s);
            }
            ++g_gl.game_count;
        }
    }

    if (g_gl.sel >= static_cast<i32>(g_gl.game_count))
        g_gl.sel = g_gl.game_count ? 0 : -1;
}

void gamelauncher_init()
{
    if (g_gl.initialized)
        return;
    g_gl.sel = -1;
    g_gl.hover = -1;
    g_gl.tab = 0;
    gamelauncher_scan();
    g_gl.initialized = true;
}

void draw_gamelauncher(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool)
{
    gamelauncher_init();

    // --- Toolbar ---
    const i32 bar_h = 40;
    r(gx, gy, gw, bar_h, kHeaderBg);
    r(gx, gy + bar_h - 1, gw, 1, kHeaderEdge);

    // Library / Settings tabs.
    const char* tabs[2] = {"Library", "Settings"};
    i32 tx = gx + 12;
    for (u32 i = 0; i < 2; ++i)
    {
        const i32 tw = 90;
        const bool active = (static_cast<i32>(i) == g_gl.tab);
        const bool hover = (mx >= tx && mx < tx + tw && my >= gy + 6 && my < gy + bar_h - 6);
        const u32 bg = active ? 0x00D0E4F4 : hover ? 0x00E8ECF0 : kHeaderBg;
        r(tx, gy + 6, tw, bar_h - 12, bg);
        t(tx + 14, gy + 12, tabs[i], 0x00202020, bg);
        tx += tw + 6;
    }

    // "Add game" button on the right.
    const i32 add_w = 100;
    const i32 add_x = gx + gw - add_w - 12;
    r(add_x, gy + 6, add_w, bar_h - 12, 0x00E0E8F0);
    r(add_x, gy + 6, add_w, 1, kHeaderEdge);
    r(add_x, gy + bar_h - 7, add_w, 1, kHeaderEdge);
    t(add_x + 12, gy + 12, "+ Add game", 0x00202020, 0x00E0E8F0);

    gy += bar_h;
    gh -= bar_h;

    if (g_gl.tab == 0)
    {
        // --- Library tab: left list + right details ---
        const i32 side_w = 260;
        r(gx, gy, side_w, gh, 0x00F8F8F8);
        r(gx + side_w - 1, gy, 1, gh, kHeaderEdge);

        if (g_gl.game_count == 0)
        {
            t(gx + 20, gy + 20, "No games installed.", 0x00606060, 0x00F8F8F8);
            t(gx + 20, gy + 44, "Copy .elf or .bin files to /disk/games/", 0x00909090, 0x00F8F8F8);
        }
        else
        {
            i32 ry = gy + 6;
            g_gl.hover = -1;
            for (u32 i = 0; i < g_gl.game_count; ++i)
            {
                const bool hover = (mx >= gx && mx < gx + side_w - 1 && my >= ry && my < ry + 48);
                if (hover)
                    g_gl.hover = static_cast<i32>(i);

                const u32 bg = (static_cast<i32>(i) == g_gl.sel) ? 0x00CCE4FC
                               : hover                           ? 0x00E0E8F0
                                                                 : 0x00F8F8F8;
                if (bg != 0x00F8F8F8)
                    r(gx + 4, ry, side_w - 8, 44, bg);

                // Game icon (a rounded square for now).
                r(gx + 12, ry + 8, 28, 28, 0x00305070);
                r(gx + 12, ry + 8, 28, 2, 0x0060A0E8);

                t(gx + 50, ry + 8, g_gl.games[i].name, 0x00202020, bg);

                char szbuf[24];
                int n = 0;
                u32 kb = g_gl.games[i].size / 1024;
                if (kb == 0)
                    szbuf[n++] = '0';
                while (kb)
                {
                    szbuf[n++] = static_cast<char>('0' + kb % 10);
                    kb /= 10;
                }
                for (int k = 0; k < n / 2; ++k)
                {
                    char t = szbuf[k];
                    szbuf[k] = szbuf[n - 1 - k];
                    szbuf[n - 1 - k] = t;
                }
                szbuf[n++] = ' ';
                szbuf[n++] = 'K';
                szbuf[n++] = 'B';
                szbuf[n] = 0;
                t(gx + 50, ry + 26, szbuf, 0x00909090, bg);
                ry += 48;
            }
        }

        // Right details pane.
        const i32 rx = gx + side_w + 8;
        const i32 rw = gw - side_w - 16;
        r(rx, gy, rw, gh, 0x00FFFFFF);

        if (g_gl.sel >= 0 && static_cast<u32>(g_gl.sel) < g_gl.game_count)
        {
            const GameEntry& ge = g_gl.games[static_cast<u32>(g_gl.sel)];
            t(rx + 20, gy + 20, ge.name, 0x00202020, 0x00FFFFFF);
            r(rx + 20, gy + 46, rw - 40, 1, kHeaderEdge);

            t(rx + 20, gy + 60, "Path:", 0x00606060, 0x00FFFFFF);
            t(rx + 90, gy + 60, ge.path, 0x00202020, 0x00FFFFFF);

            char szbuf[32];
            int n = 0;
            u32 b = ge.size;
            if (b == 0)
                szbuf[n++] = '0';
            while (b)
            {
                szbuf[n++] = static_cast<char>('0' + b % 10);
                b /= 10;
            }
            for (int k = 0; k < n / 2; ++k)
            {
                char t = szbuf[k];
                szbuf[k] = szbuf[n - 1 - k];
                szbuf[n - 1 - k] = t;
            }
            szbuf[n++] = ' ';
            szbuf[n++] = 'B';
            szbuf[n++] = 'y';
            szbuf[n++] = 't';
            szbuf[n++] = 'e';
            szbuf[n++] = 's';
            szbuf[n] = 0;
            t(rx + 20, gy + 82, "Size:", 0x00606060, 0x00FFFFFF);
            t(rx + 90, gy + 82, szbuf, 0x00202020, 0x00FFFFFF);

            // Play button.
            r(rx + 20, gy + gh - 60, 140, 36, 0x003070C0);
            t(rx + 62, gy + gh - 50, "Play", 0x00FFFFFF, 0x003070C0);
        }
        else
        {
            t(rx + 20, gy + 20, "Select a game", 0x00909090, 0x00FFFFFF);
        }
    }
    else
    {
        // --- Settings tab: per-game settings for the selected entry ---
        r(gx, gy, gw, gh, 0x00FFFFFF);
        t(gx + 20, gy + 20, "Game settings", 0x00202020, 0x00FFFFFF);
        r(gx + 20, gy + 46, gw - 40, 1, kHeaderEdge);

        if (g_gl.sel < 0)
        {
            t(gx + 20, gy + 60, "Select a game in the Library tab first.", 0x00909090, 0x00FFFFFF);
            return;
        }

        auto row = [&](const char* key, const char* value, i32 y)
        {
            t(gx + 30, y, key, 0x00404040, 0x00FFFFFF);
            r(gx + 260, y - 2, 200, 20, 0x00F0F0F0);
            r(gx + 260, y - 2, 200, 1, kHeaderEdge);
            r(gx + 260, y + 18, 200, 1, kHeaderEdge);
            t(gx + 268, y, value, 0x00202020, 0x00F0F0F0);
        };

        row("Resolution", "1280 x 720", gy + 70);
        row("VSync", "On", gy + 96);
        row("FPS cap", "60", gy + 122);
        row("Frame skip", "Auto", gy + 148);
        row("PPU interpreter", "JIT", gy + 174);
        row("SPU interpreter", "JIT", gy + 200);
        row("Translation cache", "Enabled", gy + 226);
        row("Depth buffer", "24-bit", gy + 252);
        row("Texture quality", "High", gy + 278);
        row("Anisotropic", "4x", gy + 304);

        r(gx + 30, gy + gh - 50, 160, 32, 0x003070C0);
        t(gx + 80, gy + gh - 40, "Save", 0x00FFFFFF, 0x003070C0);
    }
    (void)my;
}

void apps_bind_impl(AppRectFn rect_fn, AppTextFn text_fn) noexcept
{
    g_rect = rect_fn;
    g_text = text_fn;
    widget_bind(rect_fn, text_fn);
}

bool apps_draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept
{
    draw_explorer(gx, gy, gw, gh, mx, my, mouse_down);
    return true;
}

bool apps_draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept
{
    draw_settings(gx, gy, gw, gh, mx, my, mouse_down);
    return true;
}

bool apps_draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept
{
    draw_bin(gx, gy, gw, gh, mx, my, mouse_down);
    return true;
}

bool apps_draw_imageviewer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept
{
    draw_imageviewer(gx, gy, gw, gh, mx, my, mouse_down);
    return true;
}

void apps_load_image(const char* path) noexcept
{
    imgviewer_load_path(path);
}

bool apps_click_explorer(i32 mx, i32 my, bool pressed_edge) noexcept
{
    if (!pressed_edge)
        return false;
    explorer_init();

    // Navigation buttons.
    // We do not have exact coordinates here, so we test a small hit region
    // relative to the mouse position by recomputing the layout the same way
    // draw_toolbar does. The Explorer window is at a known size, so we
    // simply pick the button that is under the cursor when the y is in the
    // toolbar band.
    //
    // A cleaner design stores the actual rectangles from the last draw
    // pass in ExplorerState. That lands in a follow-up; for now we use
    // a bounded x range relative to the current window origin, which is
    // approximated by (mx - (window.x + 0)).

    // Double-click detection.
    const u64 now = arch::x86_64::pit_ticks();
    const bool dbl = (g_exp.hover == g_exp.last_click) && ((now - g_exp.last_click_tick) < 50) &&
                     (g_exp.hover >= 0);
    g_exp.last_click_tick = now;
    g_exp.last_click = g_exp.hover;

    if (g_exp.hover >= 0)
    {
        g_exp.sel = g_exp.hover;
        if (dbl)
        {
            const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.hover)];
            if (e.is_dir)
            {
                if (libk::strcmp(e.name, "..") == 0)
                {
                    explorer_go_up();
                }
                else
                {
                    // Build the child path.
                    char child[128];
                    u32 i = 0;
                    while (g_exp.cwd[i] && i < 120)
                    {
                        child[i] = g_exp.cwd[i];
                        ++i;
                    }
                    if (i > 0 && child[i - 1] != '/')
                    {
                        child[i++] = '/';
                    }
                    u32 j = 0;
                    while (e.name[j] && i < 127)
                    {
                        child[i++] = e.name[j++];
                    }
                    child[i] = 0;
                    explorer_navigate_to(child);
                }
            }
            else
            {
                // Open the file with the appropriate viewer.
                char child[128];
                u32 i = 0;
                while (g_exp.cwd[i] && i < 120)
                {
                    child[i] = g_exp.cwd[i];
                    ++i;
                }
                if (i > 0 && child[i - 1] != '/')
                {
                    child[i++] = '/';
                }
                u32 j = 0;
                while (e.name[j] && i < 127)
                {
                    child[i++] = e.name[j++];
                }
                child[i] = 0;

                const u32 nlen = static_cast<u32>(libk::strlen(e.name));
                if (nlen >= 4)
                {
                    const char* ext = e.name + nlen - 4;
                    if (libk::strcmp(ext, ".bmp") == 0 || libk::strcmp(ext, ".png") == 0 ||
                        libk::strcmp(ext, ".jpg") == 0)
                    {
                        apps_load_image(child);
                    }
                }
                log::write(log::Level::Info, "expl", "open: %s", child);
            }
        }
        return true;
    }
    (void)mx;
    (void)my;
    return false;
}

void apps_explorer_nav_back() noexcept
{
    explorer_init();
    explorer_go_back();
}

void apps_explorer_nav_forward() noexcept
{
    explorer_init();
    explorer_go_forward();
}

void apps_explorer_nav_up() noexcept
{
    explorer_init();
    explorer_go_up();
}

void apps_explorer_nav_refresh() noexcept
{
    explorer_init();
    explorer_refresh();
}

// Right-click handler for the Explorer window. Called by the compositor
// when the user right-clicks while an Explorer window is focused.
void apps_explorer_right_click(i32 mx, i32 my) noexcept
{
    explorer_init();
    // Find out whether the click landed on a row.
    i32 target = -1;
    for (u32 i = 0; i < g_exp.entry_count; ++i)
    {
        // Rows are laid out at y = row_start + i * 22 in window space.
        // We do not have the window origin here, so the compositor passes
        // absolute screen coordinates and we accept any y that maps to an
        // entry given the current hover computation in draw_explorer.
        (void)i;
    }

    g_exp.ctx_open = true;
    g_exp.ctx_x = mx;
    g_exp.ctx_y = my;
    g_exp.ctx_hover = -1;
    g_exp.ctx_target = target;
}

// Returns true if the explorer consumed this click (i.e. the menu was
// open and the click was inside it). Otherwise the compositor should
// close the menu and continue with normal handling.
bool apps_explorer_click_ctx(i32 mx, i32 my) noexcept
{
    if (!g_exp.ctx_open)
        return false;
    const i32 w = 180;
    const i32 h = exp_ctx_height();
    const i32 x = g_exp.ctx_x;
    const i32 y = g_exp.ctx_y;

    if (mx < x || mx >= x + w || my < y || my >= y + h)
    {
        g_exp.ctx_open = false;
        return false;
    }

    i32 cy = y;
    for (u32 i = 0; g_exp_ctx[i] != ExpCtxItem::None; ++i)
    {
        const ExpCtxItem it = g_exp_ctx[i];
        const i32 ch =
            (it == ExpCtxItem::Sep1 || it == ExpCtxItem::Sep2 || it == ExpCtxItem::Sep3) ? 6 : 24;
        if (my >= cy && my < cy + ch)
        {
            g_exp.ctx_open = false;
            if (it == ExpCtxItem::Refresh)
            {
                // Re-scan the directory.
                g_exp.initialized = false;
                explorer_init();
            }
            else if (it == ExpCtxItem::Delete)
            {
                if (g_exp.sel >= 0 && static_cast<u32>(g_exp.sel) < g_exp.entry_count)
                {
                    // Log the action; a real unlink is a syscall from the shell.
                    log::write(log::Level::Info, "expl", "delete requested: %s",
                               g_exp.entries[static_cast<u32>(g_exp.sel)].name);
                }
            }
            else if (it == ExpCtxItem::Open)
            {
                log::write(log::Level::Info, "expl", "open requested");
            }
            return true;
        }
        cy += ch;
    }
    g_exp.ctx_open = false;
    return true;
}

bool apps_click_settings(i32, i32, bool pressed_edge) noexcept
{
    if (!pressed_edge)
        return false;
    const SettingsTab tabs[7] = {SettingsTab::System, SettingsTab::Display, SettingsTab::Storage,
                                 SettingsTab::Input,  SettingsTab::Network, SettingsTab::Appearance,
                                 SettingsTab::About};
    if (g_set.hover_tab >= 0 && g_set.hover_tab < 7)
        g_set.current = tabs[static_cast<u32>(g_set.hover_tab)];

    if (g_set.current == SettingsTab::Appearance && g_set.theme_hover >= 0 &&
        g_set.theme_hover < static_cast<i32>(theme::count()))
    {
        Compositor::set_theme(static_cast<theme::Id>(g_set.theme_hover));
    }
    return true;
}

bool apps_click_bin(i32, i32, bool) noexcept
{
    return true;
}
bool apps_draw_gamelauncher(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my,
                            bool mouse_down) noexcept
{
    draw_gamelauncher(gx, gy, gw, gh, mx, my, mouse_down);
    return true;
}
} // namespace notyvos::gfx
