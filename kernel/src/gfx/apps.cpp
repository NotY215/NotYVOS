#include <kernel/acpi/acpi.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/gfx/apps.hpp>
#include <kernel/gfx/widget.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

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
        const i32 px = x + 2 + i;
        r(px, y + 1, 1, 2, 0x00305090);
        r(px, y + 11, 1, 2, 0x00305090);
        r(x + 1, y + 2 + i, 2, 1, 0x00305090);
        r(x + 11, y + 2 + i, 2, 1, 0x00305090);
    }
}

constexpr u32 kMaxEntries = 32;

struct ExplEntry
{
    char name[64];
    bool is_dir;
    u32 size;
};

struct ExplorerState
{
    bool initialized;
    ExplEntry entries[kMaxEntries];
    u32 entry_count;
    i32 sel;
    i32 hover;
    i32 nav_hover;
    char address[128];
};
ExplorerState g_exp{};

void explorer_collect()
{
    g_exp.entry_count = 0;
    auto* root = fs::vfs_root();
    if (!root)
        return;

    {
        ExplEntry& e = g_exp.entries[g_exp.entry_count];
        const char* n = "disk";
        u32 i = 0;
        while (n[i] && i < 63)
        {
            e.name[i] = n[i];
            ++i;
        }
        e.name[i] = 0;
        e.is_dir = true;
        e.size = 0;
        ++g_exp.entry_count;
    }

    for (fs::VNode* c = root->children; c && g_exp.entry_count < kMaxEntries; c = c->next)
    {
        if (libk::strcmp(c->name, "disk") == 0)
            continue;
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

void explorer_init()
{
    if (g_exp.initialized)
        return;
    explorer_collect();
    g_exp.sel = -1;
    g_exp.hover = -1;
    g_exp.nav_hover = 0;
    const char* p = "This PC";
    u32 i = 0;
    while (p[i] && i < 127)
    {
        g_exp.address[i] = p[i];
        ++i;
    }
    g_exp.address[i] = 0;
    g_exp.initialized = true;
}

enum class SettingsTab : u8
{
    System,
    Display,
    Storage,
    Input,
    Network,
    About
};

struct SettingsState
{
    bool initialized;
    SettingsTab current;
    i32 hover_tab;
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
    case SettingsTab::About:
        return "About";
    }
    return "";
}

struct BinState
{
    bool initialized;
};
BinState g_bin{};

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
    if (g_exp.nav_hover == 1)
        r(gx + 4, gy + 4, 28, 22, kToolHi);
    arrow_left(gx + 10, gy + 8);
    if (g_exp.nav_hover == 2)
        r(gx + 36, gy + 4, 28, 22, kToolHi);
    arrow_right(gx + 42, gy + 8);
    if (g_exp.nav_hover == 3)
        r(gx + 68, gy + 4, 28, 22, kToolHi);
    arrow_up(gx + 74, gy + 8);
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

void draw_navigation_pane(i32 gx, i32 gy, i32 /*gw*/, i32 gh)
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

void draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool /*down*/)
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
}

void draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool /*down*/)
{
    if (!g_set.initialized)
    {
        g_set.current = SettingsTab::System;
        g_set.hover_tab = -1;
        g_set.initialized = true;
    }

    const i32 side_w = 200;
    r(gx, gy, side_w, gh, 0x00F0F0F0);
    r(gx + side_w - 1, gy, 1, gh, kHeaderEdge);
    r(gx, gy, side_w, 40, 0x00E4E4E4);
    t(gx + 16, gy + 14, "Settings", 0x00202020, 0x00E4E4E4);

    const SettingsTab tabs[6] = {SettingsTab::System, SettingsTab::Display, SettingsTab::Storage,
                                 SettingsTab::Input,  SettingsTab::Network, SettingsTab::About};
    i32 ty = gy + 52;
    g_set.hover_tab = -1;
    for (u32 i = 0; i < 6; ++i)
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
        label_row("Build", "phase 4A");
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

void draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32 /*mx*/, i32 /*my*/, bool /*down*/)
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

} // namespace

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

bool apps_click_explorer(i32 mx, i32 my, bool pressed_edge) noexcept
{
    if (!pressed_edge)
        return false;
    explorer_init();
    if (g_exp.hover >= 0)
    {
        g_exp.sel = g_exp.hover;
        return true;
    }
    (void)mx;
    (void)my;
    return false;
}

bool apps_click_settings(i32 /*mx*/, i32 /*my*/, bool pressed_edge) noexcept
{
    if (!pressed_edge)
        return false;
    const SettingsTab tabs[6] = {SettingsTab::System, SettingsTab::Display, SettingsTab::Storage,
                                 SettingsTab::Input,  SettingsTab::Network, SettingsTab::About};
    if (g_set.hover_tab >= 0 && g_set.hover_tab < 6)
    {
        g_set.current = tabs[static_cast<u32>(g_set.hover_tab)];
    }
    return true;
}

bool apps_click_bin(i32, i32, bool) noexcept
{
    return true;
}

} // namespace notyvos::gfx
