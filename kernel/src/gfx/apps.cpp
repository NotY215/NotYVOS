extern "C" void notyvos_compositor_pump_for_modal();

#include <kernel/acpi/acpi.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/gfx/apps.hpp>
#include <kernel/gfx/clipboard.hpp>
#include <kernel/gfx/widget.hpp>
#include <kernel/img/decoder.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/net/net.hpp>
#include <kernel/net/e1000.hpp>
#include <kernel/net/wifi.hpp>

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
constexpr u32 kToolDim = 0x00B0B0B0;
constexpr u32 kAddressBg = 0x00FFFFFF;
constexpr u32 kAddressEdge = 0x00A0A0A0;
constexpr u32 kCrumbHover = 0x00CCE4FC;
constexpr u32 kNavHi = 0x00CCE4FC;
constexpr u32 kContentBg = 0x00FFFFFF;
constexpr u32 kContentFg = 0x00202020;
constexpr u32 kContentHi = 0x00CCE4FC;
constexpr u32 kHeaderBg = 0x00F0F0F0;
constexpr u32 kHeaderFg = 0x00404040;
constexpr u32 kHeaderEdge = 0x00C0C0C0;
constexpr u32 kStatusBg = 0x00F0F0F0;
constexpr u32 kStatusFg = 0x00303030;
constexpr u32 kIconFolder = 0x00FFB060;
constexpr u32 kSideBg = 0x00F3F3F3;
constexpr u32 kSideHeader = 0x00808080;
constexpr u32 kSideHover = 0x00E5F3FB;
constexpr u32 kSideSelect = 0x00CCE8FF;
constexpr u32 kSideEdge = 0x00C8C8C8;
constexpr u32 kCmdBg = 0x00F0F0F0;
constexpr u32 kCmdEdge = 0x00C8C8C8;

void icon_folder(i32 x, i32 y, i32 s)
{
    r(x + 1, y + 4, s - 6, s - 8, kIconFolder);
    r(x + 1, y + 7, s - 2, s - 9, kIconFolder);
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
void icon_large_folder(i32 x, i32 y)
{
    r(x + 4, y + 14, 40, 34, kIconFolder);
    r(x + 4, y + 20, 48, 32, kIconFolder);
    r(x + 4, y + 20, 48, 2, 0x00906020);
}
void icon_large_file(i32 x, i32 y)
{
    r(x + 12, y + 4, 28, 44, 0x00FFFFFF);
    r(x + 12, y + 4, 28, 1, 0x00A0A0A0);
    r(x + 12, y + 47, 28, 1, 0x00A0A0A0);
    r(x + 12, y + 4, 1, 44, 0x00A0A0A0);
    r(x + 39, y + 4, 1, 44, 0x00A0A0A0);
    r(x + 16, y + 14, 20, 2, 0x00C0C0C0);
    r(x + 16, y + 20, 20, 2, 0x00C0C0C0);
    r(x + 16, y + 26, 20, 2, 0x00C0C0C0);
}
void icon_large_image(i32 x, i32 y)
{
    r(x + 8, y + 8, 36, 36, 0x00FFFFFF);
    r(x + 8, y + 8, 36, 1, 0x00A0A0A0);
    r(x + 8, y + 43, 36, 1, 0x00A0A0A0);
    r(x + 8, y + 8, 1, 36, 0x00A0A0A0);
    r(x + 43, y + 8, 1, 36, 0x00A0A0A0);
    r(x + 14, y + 16, 8, 8, 0x00FFD060);
    r(x + 12, y + 28, 8, 10, 0x0060A040);
    r(x + 22, y + 24, 12, 14, 0x0060A040);
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
void chevron_right(i32 x, i32 y)
{
    for (i32 i = 0; i < 6; ++i)
        r(x + i / 2, y + i, 1, 1, 0x00606060);
    for (i32 i = 0; i < 6; ++i)
        r(x + (5 - i) / 2, y + i, 1, 1, 0x00606060);
}
void icon_view_details(i32 x, i32 y)
{
    for (i32 i = 0; i < 3; ++i)
        r(x + 2, y + 3 + i * 5, 11, 2, 0x00404040);
}
void icon_view_grid(i32 x, i32 y)
{
    for (i32 row = 0; row < 2; ++row)
        for (i32 col = 0; col < 2; ++col)
            r(x + 2 + col * 6, y + 2 + row * 6, 5, 5, 0x00404040);
}

constexpr u32 kMaxEntries = 64;
constexpr u32 kMaxHistory = 32;
constexpr u32 kMaxCrumbs = 8;
constexpr i32 kSideWidth = 180;
constexpr i32 kSideItemH = 22;
constexpr i32 kSideHeaderH = 26;

enum class ViewMode : u8
{
    Details = 0,
    Grid = 1,
};

enum class SideIcon : u8
{
    None = 0,
    Folder,
    Drive,
};

struct SideItem
{
    const char* label;
    const char* path;
    SideIcon icon;
};

const SideItem g_side_items[] = {
    {"Favorites", nullptr, SideIcon::None},     {"  Desktop", "/", SideIcon::Folder},
    {"  Downloads", "/disk", SideIcon::Folder}, {"  Recent Places", "/", SideIcon::Folder},
    {"Libraries", nullptr, SideIcon::None},     {"  Documents", "/disk", SideIcon::Folder},
    {"  Music", "/disk", SideIcon::Folder},     {"  Pictures", "/", SideIcon::Folder},
    {"  Videos", "/disk", SideIcon::Folder},    {"Computer", nullptr, SideIcon::None},
    {"  notyvos-root", "/", SideIcon::Drive},   {"  Local Disk (NYFS)", "/disk", SideIcon::Drive},
    {"Network", nullptr, SideIcon::None},
};
constexpr u32 kSideItemCount = sizeof(g_side_items) / sizeof(g_side_items[0]);

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
    char cwd[128];

    char history[kMaxHistory][128];
    u32 history_count;
    u32 history_pos;
    i32 last_click;
    u64 last_click_tick;

    ViewMode view_mode;

    bool ctx_open;
    i32 ctx_x;
    i32 ctx_y;
    i32 ctx_hover;
    i32 ctx_target;

    i32 win_x, win_y;
    u32 win_w, win_h;

    u32 crumb_count;
    char crumb_text[kMaxCrumbs][32];
    char crumb_path[kMaxCrumbs][128];
    i32 crumb_x[kMaxCrumbs];
    i32 crumb_w[kMaxCrumbs];
    i32 crumb_y;
    i32 crumb_hover;

    i32 side_hover;
    i32 side_sel;

    bool modal_result;
    char modal_input[64];
};
ExplorerState g_exp{};

void explorer_refresh();
void explorer_navigate_to(const char* abs_path);
void explorer_go_back();
void explorer_go_forward();
void explorer_go_up();
void draw_explorer_context_menu();
void draw_breadcrumb(i32 x, i32 y, i32 w);

void side_icon_draw(SideIcon ic, i32 x, i32 y)
{
    if (ic == SideIcon::Folder)
    {
        r(x, y + 3, 14, 9, kIconFolder);
        r(x, y + 5, 14, 7, 0x00FFC080);
    }
    else if (ic == SideIcon::Drive)
    {
        r(x, y + 2, 14, 10, 0x00D8D8D8);
        r(x, y + 2, 14, 1, 0x00808080);
        r(x, y + 11, 14, 1, 0x00808080);
        r(x + 3, y + 5, 8, 3, 0x00A0A0A0);
    }
}

void draw_sidebar(i32 x, i32 y, i32 w, i32 h)
{
    r(x, y, w, h, kSideBg);
    r(x + w - 1, y, 1, h, kSideEdge);

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();

    i32 cy = y + 6;
    g_exp.side_hover = -1;

    for (u32 i = 0; i < kSideItemCount; ++i)
    {
        const SideItem& it = g_side_items[i];

        if (it.path == nullptr)
        {
            t(x + 12, cy + 6, it.label, kSideHeader, kSideBg);
            r(x + 8, cy + 22, w - 16, 1, kSideEdge);
            cy += kSideHeaderH;
            continue;
        }

        const bool hover = (mx >= x && mx < x + w - 4 && my >= cy && my < cy + kSideItemH);
        const bool sel = (static_cast<i32>(i) == g_exp.side_sel);
        if (hover)
            g_exp.side_hover = static_cast<i32>(i);

        const u32 bg = sel ? kSideSelect : hover ? kSideHover : kSideBg;
        if (bg != kSideBg)
            r(x + 4, cy, w - 8, kSideItemH, bg);

        side_icon_draw(it.icon, x + 8, cy + 4);
        t(x + 28, cy + 3, it.label, 0x00202020, bg);
        cy += kSideItemH;
    }
}

void draw_command_bar(i32 x, i32 y, i32 w)
{
    const i32 h = 26;
    r(x, y, w, h, kCmdBg);
    r(x, y + h - 1, w, 1, kCmdEdge);

    t(x + 12, y + 5, "Organize", 0x00202020, kCmdBg);
    t(x + 92, y + 5, "Share with", 0x00202020, kCmdBg);
    t(x + 192, y + 5, "Burn", 0x00202020, kCmdBg);
    t(x + 242, y + 5, "New folder", 0x00202020, kCmdBg);
}

void explorer_collect()
{
    g_exp.entry_count = 0;

    auto* dir = fs::vfs_lookup(g_exp.cwd, "/");
    if (!dir)
        dir = fs::vfs_root();
    if (!dir)
        return;

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

void join_path(char* out, usize cap, const char* base, const char* leaf) noexcept
{
    usize i = 0;
    while (base[i] && i < cap - 1)
    {
        out[i] = base[i];
        ++i;
    }
    if (i > 0 && out[i - 1] != '/')
    {
        out[i++] = '/';
    }
    usize j = 0;
    while (leaf[j] && i < cap - 1)
    {
        out[i++] = leaf[j++];
    }
    out[i] = 0;
}

void explorer_navigate_to(const char* abs_path)
{
    if (!abs_path)
        return;

    if (g_exp.history_pos + 1u < g_exp.history_count)
        g_exp.history_count = g_exp.history_pos + 1u;

    if (g_exp.history_count == 0 || libk::strcmp(g_exp.history[g_exp.history_pos], abs_path) != 0)
    {
        if (g_exp.history_count >= kMaxHistory)
        {
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
    }

    u32 i = 0;
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
    char parent[128];
    u32 i = 0;
    while (g_exp.cwd[i] && i < 127)
    {
        parent[i] = g_exp.cwd[i];
        ++i;
    }
    parent[i] = 0;
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
    g_exp.view_mode = ViewMode::Details;
    g_exp.ctx_open = false;
    g_exp.ctx_hover = -1;
    g_exp.ctx_target = -1;
    g_exp.crumb_hover = -1;
    g_exp.side_hover = -1;
    g_exp.side_sel = -1;
    libk::strcpy(g_exp.address, "/");
    explorer_refresh();
    g_exp.initialized = true;
}

void modal_pump() noexcept;

bool apps_prompt_text(const char* title, const char* initial, char* out, usize cap) noexcept
{
    if (!out || cap == 0)
        return false;
    out[0] = 0;

    if (initial)
    {
        usize i = 0;
        while (initial[i] && i + 1 < cap)
        {
            out[i] = initial[i];
            ++i;
        }
        out[i] = 0;
    }
    usize len = libk::strlen(out);

    u64 frames = 0;
    const u64 max_frames = 200;

    for (;;)
    {
        const i32 sw = static_cast<i32>(fb::Framebuffer::width());
        const i32 sh = static_cast<i32>(fb::Framebuffer::height());
        const i32 w = 400, h = 140;
        const i32 x = (sw - w) / 2, y = (sh - h) / 2;

        r(x - 2, y - 2, w + 4, h + 4, 0x00000000);
        r(x, y, w, h, 0x00F0F0F0);
        r(x, y, w, 1, 0x00606060);
        r(x, y + h - 1, w, 1, 0x00606060);
        r(x, y, 1, h, 0x00606060);
        r(x + w - 1, y, 1, h, 0x00606060);

        t(x + 16, y + 16, title, 0x00202020, 0x00F0F0F0);

        r(x + 16, y + 48, w - 32, 26, 0x00FFFFFF);
        r(x + 16, y + 48, w - 32, 1, 0x00A0A0A0);
        r(x + 16, y + 73, w - 32, 1, 0x00A0A0A0);
        r(x + 16, y + 48, 1, 26, 0x00A0A0A0);
        r(x + w - 17, y + 48, 1, 26, 0x00A0A0A0);
        t(x + 22, y + 52, out, 0x00202020, 0x00FFFFFF);

        t(x + 16, y + 90, "Enter to confirm, Esc to cancel", 0x00606060, 0x00F0F0F0);

        i32 c = arch::x86_64::keyboard_pop();
        while (c >= 0)
        {
            const char ch = static_cast<char>(c);
            if (ch == '\n' || ch == '\r')
            {
                return true;
            }
            if (ch == 27)
            {
                out[0] = 0;
                return false;
            }
            if (ch == '\b' || ch == 127)
            {
                if (len > 0)
                {
                    --len;
                    out[len] = 0;
                }
            }
            else if (ch >= 0x20 && ch <= 0x7E && len + 1 < cap)
            {
                out[len++] = ch;
                out[len] = 0;
            }
            c = arch::x86_64::keyboard_pop();
        }

        modal_pump();
        if (++frames > max_frames)
        {
            out[0] = 0;
            return false;
        }
    }
}

bool apps_prompt_confirm(const char* title, const char* message) noexcept
{
    u64 frames = 0;
    const u64 max_frames = 200;

    for (;;)
    {
        const i32 sw = static_cast<i32>(fb::Framebuffer::width());
        const i32 sh = static_cast<i32>(fb::Framebuffer::height());
        const i32 w = 420, h = 140;
        const i32 x = (sw - w) / 2, y = (sh - h) / 2;

        r(x - 2, y - 2, w + 4, h + 4, 0x00000000);
        r(x, y, w, h, 0x00F0F0F0);
        r(x, y, w, 1, 0x00606060);
        r(x, y + h - 1, w, 1, 0x00606060);
        r(x, y, 1, h, 0x00606060);
        r(x + w - 1, y, 1, h, 0x00606060);

        t(x + 16, y + 16, title, 0x00202020, 0x00F0F0F0);
        t(x + 16, y + 48, message, 0x00404040, 0x00F0F0F0);
        t(x + 16, y + 100, "Y = yes, N or Esc = no", 0x00606060, 0x00F0F0F0);

        i32 c = arch::x86_64::keyboard_pop();
        while (c >= 0)
        {
            const char ch = static_cast<char>(c);
            if (ch == 'y' || ch == 'Y')
                return true;
            if (ch == 'n' || ch == 'N' || ch == 27)
                return false;
            c = arch::x86_64::keyboard_pop();
        }

        modal_pump();
        if (++frames > max_frames)
            return false;
    }
}

void modal_pump() noexcept
{
    notyvos_compositor_pump_for_modal();

    for (u32 i = 0; i < 400000u; ++i)
        asm volatile("pause");
}

void draw_toolbar(i32 gx, i32 gy, i32 gw)
{
    const i32 h = 30;
    r(gx, gy, gw, h, kToolBg);
    r(gx, gy + h - 1, gw, 1, kToolEdge);

    const bool can_back = (g_exp.history_pos > 0);
    const bool can_fwd = (g_exp.history_pos + 1u < g_exp.history_count);
    const bool can_up = (libk::strcmp(g_exp.cwd, "/") != 0);

    if (g_exp.nav_hover == 1 && can_back)
        r(gx + 4, gy + 4, 28, 22, kToolHi);
    arrow_left(gx + 10, gy + 8);
    if (!can_back)
        r(gx + 4, gy + 4, 28, 22, kToolDim);
    if (g_exp.nav_hover == 2 && can_fwd)
        r(gx + 36, gy + 4, 28, 22, kToolHi);
    arrow_right(gx + 42, gy + 8);
    if (!can_fwd)
        r(gx + 36, gy + 4, 28, 22, kToolDim);
    if (g_exp.nav_hover == 3 && can_up)
        r(gx + 68, gy + 4, 28, 22, kToolHi);
    arrow_up(gx + 74, gy + 8);
    if (!can_up)
        r(gx + 68, gy + 4, 28, 22, kToolDim);
    if (g_exp.nav_hover == 4)
        r(gx + 100, gy + 4, 28, 22, kToolHi);
    arrow_refresh(gx + 106, gy + 8);

    if (g_exp.nav_hover == 5)
        r(gx + 132, gy + 4, 28, 22, kToolHi);
    if (g_exp.view_mode == ViewMode::Details)
        icon_view_grid(gx + 140, gy + 8);
    else
        icon_view_details(gx + 140, gy + 8);

    draw_breadcrumb(gx + 168, gy + 5, gw - 168 - 8);
}

void draw_breadcrumb(i32 x, i32 y, i32 w)
{
    const i32 h = 20;
    r(x, y, w, h, kAddressBg);
    r(x, y, w, 1, kAddressEdge);
    r(x, y + h - 1, w, 1, kAddressEdge);
    r(x, y, 1, h, kAddressEdge);
    r(x + w - 1, y, 1, h, kAddressEdge);

    g_exp.crumb_count = 0;
    g_exp.crumb_y = y;
    g_exp.crumb_hover = -1;

    auto push_crumb = [&](const char* text, const char* path)
    {
        if (g_exp.crumb_count >= kMaxCrumbs)
            return;
        const u32 idx = g_exp.crumb_count++;
        u32 i = 0;
        while (text[i] && i < 31)
        {
            g_exp.crumb_text[idx][i] = text[i];
            ++i;
        }
        g_exp.crumb_text[idx][i] = 0;
        i = 0;
        while (path[i] && i < 127)
        {
            g_exp.crumb_path[idx][i] = path[i];
            ++i;
        }
        g_exp.crumb_path[idx][i] = 0;
    };

    push_crumb("root", "/");

    const char* p = g_exp.cwd;
    while (*p == '/')
        ++p;
    while (*p)
    {
        const char* seg_start = p;
        while (*p && *p != '/')
            ++p;
        const usize seg_len = static_cast<usize>(p - seg_start);
        if (seg_len > 0)
        {
            char seg[32];
            const usize copy = (seg_len < 31) ? seg_len : 31;
            for (usize k = 0; k < copy; ++k)
                seg[k] = seg_start[k];
            seg[copy] = 0;

            char cum[128];
            const usize prefix = static_cast<usize>(seg_start - g_exp.cwd);
            const usize prefix_copy = (prefix < 127) ? prefix : 127;
            for (usize k = 0; k < prefix_copy; ++k)
                cum[k] = g_exp.cwd[k];
            cum[prefix_copy] = 0;
            if (prefix_copy == 0)
            {
                cum[0] = '/';
                cum[1] = 0;
            }

            push_crumb(seg, cum);
        }
        while (*p == '/')
            ++p;
    }

    i32 cx = x + 4;
    const i32 cy = y + 2;
    for (u32 i = 0; i < g_exp.crumb_count; ++i)
    {
        const u32 len = static_cast<u32>(libk::strlen(g_exp.crumb_text[i]));
        const i32 cw = static_cast<i32>(len) * static_cast<i32>(kCellW) + 12;
        const i32 cend = cx + cw;

        g_exp.crumb_x[i] = cx;
        g_exp.crumb_w[i] = cw;

        const i32 mx = arch::x86_64::mouse_x();
        const i32 my = arch::x86_64::mouse_y();
        const bool is_hover = (mx >= cx && mx < cend && my >= y && my < y + h);
        if (is_hover)
            g_exp.crumb_hover = static_cast<i32>(i);

        if (is_hover)
        {
            r(cx, cy, cw - 2, h - 4, kCrumbHover);
        }

        t(cx + 6, cy + 1, g_exp.crumb_text[i], kMenuFg, is_hover ? kCrumbHover : kAddressBg);
        cx += cw;

        if (i + 1 < g_exp.crumb_count)
        {
            chevron_right(cx + 2, cy + 5);
            cx += 10;
        }
    }
    (void)w;
}

void draw_explorer_status(i32 gx, i32 gy, i32 gw)
{
    const i32 h = 22;
    r(gx, gy, gw, h, kStatusBg);
    r(gx, gy, gw, 1, kHeaderEdge);

    char buf[64];
    int n = 0;
    u32 c = g_exp.entry_count;
    if (c == 0)
        buf[n++] = '0';
    while (c)
    {
        buf[n++] = static_cast<char>('0' + c % 10);
        c /= 10;
    }
    for (int i = 0; i < n / 2; ++i)
    {
        const char tmp = buf[i];
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

    if (g_exp.sel >= 0 && static_cast<u32>(g_exp.sel) < g_exp.entry_count)
    {
        const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.sel)];
        char info[96];
        int m = 0;
        for (u32 i = 0; e.name[i] && m < 40; ++i)
            info[m++] = e.name[i];
        if (!e.is_dir)
        {
            info[m++] = ' ';
            info[m++] = ' ';
            u32 v = e.size;
            char num[16];
            int nn = 0;
            if (v == 0)
                num[nn++] = '0';
            while (v)
            {
                num[nn++] = static_cast<char>('0' + v % 10);
                v /= 10;
            }
            for (int k = 0; k < nn / 2; ++k)
            {
                const char tmp = num[k];
                num[k] = num[nn - 1 - k];
                num[nn - 1 - k] = tmp;
            }
            for (int k = 0; k < nn; ++k)
                info[m++] = num[k];
            info[m++] = ' ';
            info[m++] = 'B';
        }
        info[m] = 0;
        t(gx + gw - 12 - static_cast<i32>(m) * static_cast<i32>(kCellW), gy + 3, info, kStatusFg,
          kStatusBg);
    }
}

void draw_content_details(i32 cx, i32 cy, i32 cw, i32 ch)
{
    const i32 header_h = 22;
    r(cx, cy, cw, header_h, kHeaderBg);
    r(cx, cy + header_h - 1, cw, 1, kHeaderEdge);
    t(cx + 40, cy + 3, "Name", kHeaderFg, kHeaderBg);
    t(cx + cw - 200, cy + 3, "Type", kHeaderFg, kHeaderBg);
    t(cx + cw - 90, cy + 3, "Size", kHeaderFg, kHeaderBg);

    const i32 row_h = 22;
    i32 ry = cy + header_h + 2;
    g_exp.hover = -1;
    for (u32 i = 0; i < g_exp.entry_count; ++i)
    {
        if (ry + row_h > cy + ch - 22)
            break;

        const i32 mx = arch::x86_64::mouse_x();
        const i32 my = arch::x86_64::mouse_y();
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
                const char tmp = sb[k];
                sb[k] = sb[n - 1 - k];
                sb[n - 1 - k] = tmp;
            }
            sb[n] = 0;
            t(cx + cw - 90, ry + 3, sb, kContentFg, bg);
        }
        ry += row_h;
    }
}

void draw_content_grid(i32 cx, i32 cy, i32 cw, i32 ch)
{
    const i32 cell_w = 100;
    const i32 cell_h = 96;
    const i32 pad_x = 8;
    const i32 pad_y = 8;
    const i32 cols = (cw - pad_x * 2) / cell_w;
    if (cols <= 0)
        return;

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();

    g_exp.hover = -1;
    for (u32 i = 0; i < g_exp.entry_count; ++i)
    {
        const i32 col = static_cast<i32>(i) % cols;
        const i32 row = static_cast<i32>(i) / cols;
        const i32 x = cx + pad_x + col * cell_w;
        const i32 y = cy + pad_y + row * cell_h;
        if (y + cell_h > cy + ch - 22)
            break;

        const bool hover = (mx >= x && mx < x + cell_w && my >= y && my < y + cell_h);
        if (hover)
            g_exp.hover = static_cast<i32>(i);

        const u32 bg = (static_cast<i32>(i) == g_exp.sel) ? kContentHi
                       : hover                            ? kContentHi
                                                          : kContentBg;
        if (bg != kContentBg)
            r(x + 2, y + 2, cell_w - 4, cell_h - 4, bg);

        const ExplEntry& e = g_exp.entries[i];
        const i32 icon_w = 64;
        const i32 ix = x + (cell_w - icon_w) / 2;
        const i32 iy = y + 8;
        if (e.is_dir)
        {
            icon_large_folder(ix, iy);
        }
        else if (libk::strcmp(e.name, "wallpaper.raw") == 0)
        {
            icon_large_image(ix, iy);
        }
        else
        {
            icon_large_file(ix, iy);
        }

        const i32 label_y = y + 60;
        u32 name_len = static_cast<u32>(libk::strlen(e.name));
        const u32 max_chars = (cell_w - 8) / kCellW;
        if (name_len > max_chars)
            name_len = max_chars;
        char label[64];
        for (u32 k = 0; k < name_len; ++k)
            label[k] = e.name[k];
        if (name_len == max_chars && libk::strlen(e.name) > max_chars)
        {
            if (name_len >= 2)
            {
                label[name_len - 1] = '.';
                label[name_len] = 0;
            }
            else
                label[name_len] = 0;
        }
        else
        {
            label[name_len] = 0;
        }
        const i32 label_w = static_cast<i32>(name_len) * static_cast<i32>(kCellW);
        t(x + (cell_w - label_w) / 2, label_y, label, kContentFg, bg);
    }
}

void draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32, i32, bool)
{
    explorer_init();

    g_exp.win_x = gx;
    g_exp.win_y = gy;
    g_exp.win_w = static_cast<u32>(gw);
    g_exp.win_h = static_cast<u32>(gh);

    const i32 menu_h = 22;
    r(gx, gy, gw, menu_h, kMenuBg);
    r(gx, gy + menu_h - 1, gw, 1, kHeaderEdge);
    const char* items[3] = {"File", "Edit", "View"};
    i32 mx_items = gx + 8;
    for (const char* s : items)
    {
        const u32 len = static_cast<u32>(libk::strlen(s));
        t(mx_items, gy + 3, s, kMenuFg, kMenuBg);
        mx_items += static_cast<i32>(len) * static_cast<i32>(kCellW) + 20;
    }

    gy += menu_h;
    gh -= menu_h;

    draw_toolbar(gx, gy, gw);
    gy += 30;
    gh -= 30;

    draw_command_bar(gx, gy, gw);
    gy += 26;
    gh -= 26;

    draw_sidebar(gx, gy, kSideWidth, gh - 22);

    const i32 cx = gx + kSideWidth;
    const i32 cw = gw - kSideWidth;
    r(cx, gy, cw, gh, kContentBg);

    if (g_exp.view_mode == ViewMode::Details)
    {
        draw_content_details(cx, gy, cw, gh);
    }
    else
    {
        draw_content_grid(cx, gy, cw, gh);
    }

    draw_explorer_status(gx, gy + gh - 22, gw);
    draw_explorer_context_menu();
}

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
        return "New file";
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

    const i32 mx = arch::x86_64::mouse_x();
    const i32 my = arch::x86_64::mouse_y();

    i32 cy = y;
    g_exp.ctx_hover = -1;
    for (u32 i = 0; g_exp_ctx[i] != ExpCtxItem::None; ++i)
    {
        const ExpCtxItem it = g_exp_ctx[i];
        if (it == ExpCtxItem::Sep1 || it == ExpCtxItem::Sep2 || it == ExpCtxItem::Sep3)
        {
            r(x + 8, cy + 2, w - 16, 1, 0x00D0D0D0);
            cy += 6;
            continue;
        }
        const bool hover = (mx >= x && mx < x + w && my >= cy && my < cy + 24);
        if (hover)
            g_exp.ctx_hover = static_cast<i32>(i);
        const u32 bg = hover ? 0x00CCE4FC : 0x00F8F8F8;
        if (hover)
            r(x + 2, cy + 1, w - 4, 22, bg);

        const bool need_sel = (it == ExpCtxItem::Open || it == ExpCtxItem::Rename ||
                               it == ExpCtxItem::Delete || it == ExpCtxItem::Properties);
        const u32 fg = (need_sel && g_exp.sel < 0) ? 0x00A0A0A0 : 0x00202020;
        t(x + 16, cy + 4, exp_ctx_label(it), fg, bg);
        cy += 24;
    }
}

void exp_action_open()
{
    if (g_exp.sel < 0 || static_cast<u32>(g_exp.sel) >= g_exp.entry_count)
        return;
    const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.sel)];

    if (e.is_dir)
    {
        if (libk::strcmp(e.name, "..") == 0)
        {
            explorer_go_up();
        }
        else
        {
            char child[128];
            join_path(child, sizeof(child), g_exp.cwd, e.name);
            explorer_navigate_to(child);
        }
        return;
    }

    char child[128];
    join_path(child, sizeof(child), g_exp.cwd, e.name);
    const u32 nlen = static_cast<u32>(libk::strlen(e.name));
    if (nlen >= 4)
    {
        const char* ext = e.name + nlen - 4;
        if (libk::strcmp(ext, ".bmp") == 0 || libk::strcmp(ext, ".png") == 0 ||
            libk::strcmp(ext, ".jpg") == 0 || libk::strcmp(ext, ".gif") == 0)
        {
            apps_load_image(child);
            return;
        }
    }
    log::write(log::Level::Info, "expl", "open: %s (no handler)", child);
}

void exp_action_new()
{
    const bool is_disk = (libk::strcmp(g_exp.cwd, "/disk") == 0);
    if (!is_disk)
    {
        (void)apps_prompt_confirm("New file",
                                  "Only /disk supports creating files. Switch to /disk first.");
        return;
    }

    char name[64] = "newfile.txt";
    if (!apps_prompt_text("New file name", name, name, sizeof(name)))
        return;
    if (name[0] == 0)
        return;

    auto* parent = fs::vfs_lookup("/disk", "/");
    if (!parent)
    {
        log::write(log::Level::Warn, "expl", "new: /disk not mounted");
        return;
    }
    const int rc = fs::vfs_create(parent, name);
    log::write(log::Level::Info, "expl", "create('%s') = %d", name, rc);
    explorer_refresh();
}

void exp_action_refresh()
{
    explorer_refresh();
}

void exp_action_rename()
{
    if (g_exp.sel < 0 || static_cast<u32>(g_exp.sel) >= g_exp.entry_count)
        return;
    const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.sel)];
    if (libk::strcmp(e.name, "..") == 0)
        return;

    auto* node = fs::vfs_lookup(g_exp.cwd, "/");
    if (!node)
        return;
    auto* child = fs::vnode_find_child(node, e.name);
    if (!child)
        return;

    char new_name[64];
    u32 i = 0;
    while (e.name[i] && i < 63)
    {
        new_name[i] = e.name[i];
        ++i;
    }
    new_name[i] = 0;

    if (!apps_prompt_text("Rename", new_name, new_name, sizeof(new_name)))
        return;
    if (new_name[0] == 0 || libk::strcmp(new_name, e.name) == 0)
        return;

    const int rc = fs::vfs_rename(child, new_name);
    log::write(log::Level::Info, "expl", "rename('%s' -> '%s') = %d", e.name, new_name, rc);
    explorer_refresh();
}

void exp_action_delete()
{
    if (g_exp.sel < 0 || static_cast<u32>(g_exp.sel) >= g_exp.entry_count)
        return;
    const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.sel)];
    if (libk::strcmp(e.name, "..") == 0)
        return;

    auto* node = fs::vfs_lookup(g_exp.cwd, "/");
    if (!node)
        return;
    auto* child = fs::vnode_find_child(node, e.name);
    if (!child)
        return;

    char msg[128];
    u32 n = 0;
    const char* pre = "Delete '";
    while (*pre && n < 120)
        msg[n++] = *pre++;
    u32 i = 0;
    while (e.name[i] && n < 120)
        msg[n++] = e.name[i++];
    if (n < 120)
        msg[n++] = '\'';
    if (n < 120)
        msg[n++] = '?';
    msg[n] = 0;

    if (!apps_prompt_confirm("Confirm delete", msg))
        return;

    const int rc = fs::vfs_unlink(child);
    log::write(log::Level::Info, "expl", "unlink('%s') = %d", e.name, rc);
    explorer_refresh();
}

void exp_action_properties()
{
    if (g_exp.sel < 0 || static_cast<u32>(g_exp.sel) >= g_exp.entry_count)
        return;
    const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.sel)];

    char msg[192];
    int n = 0;
    auto app = [&](const char* s)
    {
        while (*s && n < 180)
            msg[n++] = *s++;
    };
    app("Name: ");
    app(e.name);
    app("\nType: ");
    app(e.is_dir ? "Folder" : "File");
    if (!e.is_dir)
    {
        app("\nSize: ");
        char num[16];
        int nn = 0;
        u32 v = e.size;
        if (v == 0)
            num[nn++] = '0';
        while (v)
        {
            num[nn++] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        for (int k = nn - 1; k >= 0; --k)
            msg[n++] = num[k];
        app(" bytes");
    }
    msg[n] = 0;

    (void)apps_prompt_confirm("Properties", msg);
}

void exp_ctx_invoke(ExpCtxItem it)
{
    switch (it)
    {
    case ExpCtxItem::Open:
        exp_action_open();
        break;
    case ExpCtxItem::NewFolder:
        exp_action_new();
        break;
    case ExpCtxItem::Refresh:
        exp_action_refresh();
        break;
    case ExpCtxItem::Rename:
        exp_action_rename();
        break;
    case ExpCtxItem::Delete:
        exp_action_delete();
        break;
    case ExpCtxItem::Properties:
        exp_action_properties();
        break;
    default:
        break;
    }
}

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

struct BinState
{
    bool initialized;
};
BinState g_bin{};

struct ImgViewerState
{
    bool initialized;
    img::Image image;
    char name[64];
    i32 pan_x;
    i32 pan_y;
};
ImgViewerState g_img{};

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

void draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool)
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
        label_row("Kernel", "x86-64, C++20");
        label_row("Heap", "64 MiB");
        break;
    case SettingsTab::Display:
        label_row("Resolution", "800 x 600");
        label_row("Colour depth", "32-bit ARGB");
        break;
    case SettingsTab::Storage:
        label_row("Device", "sda (AHCI)");
        label_row("Filesystem", "NYFS");
        label_row("Mount point", "/disk");
        break;
    case SettingsTab::Input:
        label_row("Keyboard", "PS/2 i8042, IRQ1");
        label_row("Mouse", "PS/2, IRQ12, wheel, 200 Hz");
        break;
    case SettingsTab::Network:
    {
        char macbuf[24] = "(none)";
        char ipbuf[20] = "0.0.0.0";
        char maskbuf[20] = "0.0.0.0";
        char gwbuf[20] = "0.0.0.0";
        const char* link = "down";

        auto* e1000_if = net::e1000_interface();
        if (e1000_if)
        {
            net::format_mac(e1000_if->mac, macbuf);
            net::format_ip(e1000_if->ip, ipbuf);
            net::format_ip(e1000_if->netmask, maskbuf);
            net::format_ip(e1000_if->gateway, gwbuf);
            link = (e1000_if->state == net::IfState::Up) ? "up" : "down";
        }

        label_row("Interface", "eth0 (e1000)");
        label_row("Link", link);
        label_row("MAC", macbuf);
        label_row("IPv4", ipbuf);
        label_row("Netmask", maskbuf);
        label_row("Gateway", gwbuf);
        label_row("Wi-Fi", "no adapter");
        label_row("DNS", "(none)");
                const u32 na = net::wifi::adapter_count();
        if (na == 0)
        {
            label_row("Wi-Fi", "no adapter");
        }
        else
        {
            for (u32 i = 0; i < na; ++i)
            {
                auto* a = net::wifi::adapter_by_index(i);
                if (!a)
                    continue;
                char row[64];
                u32 p = 0;
                const char* n = a->name;
                while (*n && p < 40)
                    row[p++] = *n++;
                row[p++] = ':';
                row[p++] = ' ';
                const char* st = a->connected ? "connected" : "idle";
                while (*st && p < 60)
                    row[p++] = *st++;
                row[p] = 0;
                label_row("Wi-Fi", row);
            }
        }
        break;
    }
    case SettingsTab::Appearance:
        t(rx + 16, cy, "Appearance settings land here.", 0x00606070, kContentBg);
        break;
    case SettingsTab::About:
        t(rx + 16, cy, "NOTYVOS", 0x00202020, kContentBg);
        cy += 22;
        t(rx + 16, cy, "AGPL-3.0-or-later", 0x00404040, kContentBg);
        break;
    }
}

void draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32, i32, bool)
{
    if (!g_bin.initialized)
        g_bin.initialized = true;
    r(gx, gy, gw, gh, kContentBg);
    t(gx + 20, gy + 20, "Recycle Bin is empty.", 0x00606060, kContentBg);
}

void draw_imageviewer(i32 gx, i32 gy, i32 gw, i32 gh, i32, i32, bool)
{
    if (!g_img.initialized)
    {
        g_img.initialized = true;
        imgviewer_load_path("/wallpaper.raw");
    }

    const i32 bar_h = 22;
    r(gx, gy, gw, bar_h, kHeaderBg);
    r(gx, gy + bar_h - 1, gw, 1, kHeaderEdge);
    t(gx + 8, gy + 4, g_img.name[0] ? g_img.name : "No image loaded", kHeaderFg, kHeaderBg);

    r(gx, gy + bar_h, gw, gh - bar_h, 0x00202028);
    if (g_img.image.pixels && g_img.image.width > 0)
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
        const i32 ox = gx + (gw - dw) / 2;
        const i32 oy = gy + bar_h + (gh - bar_h - dh) / 2;

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
    }
}

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
    i32 tab;
};
GameLauncherState g_gl{};

void gamelauncher_scan()
{
    g_gl.game_count = 0;
    const char* dirs[2] = {"/disk", "/games"};
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

void draw_gamelauncher(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool)
{
    if (!g_gl.initialized)
    {
        g_gl.sel = -1;
        g_gl.hover = -1;
        g_gl.tab = 0;
        gamelauncher_scan();
        g_gl.initialized = true;
    }

    const i32 bar_h = 40;
    r(gx, gy, gw, bar_h, kHeaderBg);
    r(gx, gy + bar_h - 1, gw, 1, kHeaderEdge);

    const char* tabs[2] = {"Library", "Settings"};
    i32 tx = gx + 12;
    for (u32 i = 0; i < 2; ++i)
    {
        const i32 tw = 90;
        const bool active = (static_cast<i32>(i) == g_gl.tab);
        const u32 bg = active ? 0x00D0E4F4 : kHeaderBg;
        r(tx, gy + 6, tw, bar_h - 12, bg);
        t(tx + 14, gy + 12, tabs[i], 0x00202020, bg);
        tx += tw + 6;
    }

    gy += bar_h;
    gh -= bar_h;
    if (g_gl.tab == 0)
    {
        const i32 side_w = 260;
        r(gx, gy, side_w, gh, 0x00F8F8F8);
        r(gx + side_w - 1, gy, 1, gh, kHeaderEdge);
        if (g_gl.game_count == 0)
        {
            t(gx + 20, gy + 20, "No games found.", 0x00606060, 0x00F8F8F8);
            t(gx + 20, gy + 44, "Copy .elf or .bin to /disk.", 0x00909090, 0x00F8F8F8);
        }
        else
        {
            i32 ry = gy + 6;
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
                r(gx + 12, ry + 8, 28, 28, 0x00305070);
                t(gx + 50, ry + 8, g_gl.games[i].name, 0x00202020, bg);
                ry += 48;
            }
        }
        const i32 rx = gx + side_w + 8;
        r(rx, gy, gw - side_w - 8, gh, 0x00FFFFFF);
        if (g_gl.sel >= 0 && static_cast<u32>(g_gl.sel) < g_gl.game_count)
            t(rx + 20, gy + 20, g_gl.games[static_cast<u32>(g_gl.sel)].name, 0x00202020,
              0x00FFFFFF);
    }
    else
    {
        r(gx, gy, gw, gh, 0x00FFFFFF);
        t(gx + 20, gy + 20, "Game settings", 0x00202020, 0x00FFFFFF);
    }
}

} // namespace

void apps_bind_impl(AppRectFn rect_fn, AppTextFn text_fn) noexcept
{
    g_rect = rect_fn;
    g_text = text_fn;
    widget_bind(rect_fn, text_fn);
}

bool apps_draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool down) noexcept
{
    draw_explorer(gx, gy, gw, gh, mx, my, down);
    return true;
}

bool apps_draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool down) noexcept
{
    draw_settings(gx, gy, gw, gh, mx, my, down);
    return true;
}

bool apps_draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool down) noexcept
{
    draw_bin(gx, gy, gw, gh, mx, my, down);
    return true;
}

bool apps_draw_imageviewer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool down) noexcept
{
    draw_imageviewer(gx, gy, gw, gh, mx, my, down);
    return true;
}

bool apps_draw_gamelauncher(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool down) noexcept
{
    draw_gamelauncher(gx, gy, gw, gh, mx, my, down);
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

    const i32 rx = mx - g_exp.win_x;
    const i32 ry = my - g_exp.win_y;

    const i32 side_top = 22 + 30 + 26;

    if (rx >= 0 && rx < kSideWidth && ry >= side_top)
    {
        i32 cy = g_exp.win_y + side_top + 6;
        for (u32 i = 0; i < kSideItemCount; ++i)
        {
            const SideItem& it = g_side_items[i];
            if (it.path == nullptr)
            {
                cy += kSideHeaderH;
                continue;
            }
            if (my >= cy && my < cy + kSideItemH)
            {
                g_exp.side_sel = static_cast<i32>(i);
                explorer_navigate_to(it.path);
                return true;
            }
            cy += kSideItemH;
        }
        return false;
    }

    if (ry >= 22 && ry < 22 + 30)
    {
        if (rx >= 4 && rx < 32)
        {
            explorer_go_back();
            return true;
        }
        if (rx >= 36 && rx < 64)
        {
            explorer_go_forward();
            return true;
        }
        if (rx >= 68 && rx < 96)
        {
            explorer_go_up();
            return true;
        }
        if (rx >= 100 && rx < 128)
        {
            explorer_refresh();
            return true;
        }
        if (rx >= 132 && rx < 160)
        {
            g_exp.view_mode =
                (g_exp.view_mode == ViewMode::Details) ? ViewMode::Grid : ViewMode::Details;
            return true;
        }

        for (u32 i = 0; i < g_exp.crumb_count; ++i)
        {
            if (mx >= g_exp.crumb_x[i] && mx < g_exp.crumb_x[i] + g_exp.crumb_w[i])
            {
                explorer_navigate_to(g_exp.crumb_path[i]);
                return true;
            }
        }
    }

    if (g_exp.hover >= 0)
    {
        const u64 now = arch::x86_64::pit_ticks();
        const bool dbl = (g_exp.hover == g_exp.last_click) && ((now - g_exp.last_click_tick) < 50);
        g_exp.last_click_tick = now;
        g_exp.last_click = g_exp.hover;
        g_exp.sel = g_exp.hover;

        if (dbl)
            exp_action_open();
        return true;
    }

    return false;
}

bool apps_click_settings(i32 mx, i32 my, bool pressed_edge) noexcept
{
    if (!pressed_edge)
        return false;

    const SettingsTab tabs[7] = {SettingsTab::System, SettingsTab::Display, SettingsTab::Storage,
                                 SettingsTab::Input,  SettingsTab::Network, SettingsTab::Appearance,
                                 SettingsTab::About};

    if (g_set.hover_tab >= 0 && g_set.hover_tab < 7)
    {
        g_set.current = tabs[static_cast<u32>(g_set.hover_tab)];
        return true;
    }

    (void)mx;
    (void)my;
    return true;
}

bool apps_click_bin(i32, i32, bool) noexcept
{
    return true;
}

void apps_explorer_right_click(i32 mx, i32 my) noexcept
{
    explorer_init();
    g_exp.ctx_open = true;
    g_exp.ctx_x = mx;
    g_exp.ctx_y = my;
    g_exp.ctx_hover = -1;
    g_exp.ctx_target = g_exp.hover;
    g_exp.sel = g_exp.hover;
}

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
            exp_ctx_invoke(it);
            return true;
        }
        cy += ch;
    }
    g_exp.ctx_open = false;
    return true;
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

void apps_explorer_clipboard_copy(bool cut) noexcept
{
    explorer_init();
    if (g_exp.sel < 0 || static_cast<u32>(g_exp.sel) >= g_exp.entry_count)
        return;
    const ExplEntry& e = g_exp.entries[static_cast<u32>(g_exp.sel)];
    if (libk::strcmp(e.name, "..") == 0)
        return;

    char path[128];
    join_path(path, sizeof(path), g_exp.cwd, e.name);
    clipboard::set_file(path, cut);
    log::write(log::Level::Info, "clip", "%s: %s", cut ? "cut" : "copy", path);
}

void apps_explorer_clipboard_paste() noexcept
{
    explorer_init();

    const auto& c = clipboard::get();
    if (c.kind != clipboard::Kind::File || c.text[0] == 0)
        return;

    const char* src = c.text;

    char src_dir[128];
    char src_name[64];
    {
        usize i = 0;
        while (src[i] && i < sizeof(src_dir) - 1)
        {
            src_dir[i] = src[i];
            ++i;
        }
        src_dir[i] = 0;
        while (i > 1 && src_dir[i - 1] != '/')
            --i;
        if (i > 0)
        {
            src_dir[i - 1] = 0;
        }
        const char* leaf = src;
        const char* q = src;
        while (*q)
        {
            if (*q == '/')
                leaf = q + 1;
            ++q;
        }
        u32 k = 0;
        while (leaf[k] && k < sizeof(src_name) - 1)
        {
            src_name[k] = leaf[k];
            ++k;
        }
        src_name[k] = 0;
    }
    (void)src_dir;

    if (libk::strcmp(g_exp.cwd, "/disk") != 0)
    {
        log::write(log::Level::Warn, "clip", "paste: destination '%s' is read-only", g_exp.cwd);
        return;
    }

    auto* src_vn = fs::vfs_lookup(src, "/");
    if (!src_vn || !src_vn->ops || !src_vn->ops->read || !src_vn->ops->size)
    {
        log::write(log::Level::Warn, "clip", "paste: source '%s' not readable", src);
        return;
    }
    const isize sz = src_vn->ops->size(src_vn);
    if (sz < 0 || sz > 8 * 1024 * 1024)
        return;

    auto* bytes = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz ? sz : 1)));
    if (!bytes)
        return;
    isize got = 0;
    while (got < sz)
    {
        const isize n = src_vn->ops->read(src_vn, bytes + got, static_cast<usize>(got),
                                          static_cast<usize>(sz - got));
        if (n <= 0)
            break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(bytes);
        return;
    }

    char dst_name[80];
    u32 n = 0;
    while (src_name[n] && n < 63)
    {
        dst_name[n] = src_name[n];
        ++n;
    }
    dst_name[n] = 0;

    auto* dest_dir = fs::vfs_lookup("/disk", "/");
    if (!dest_dir)
    {
        mm::Heap::deallocate(bytes);
        return;
    }

    if (fs::vnode_find_child(dest_dir, dst_name))
    {
        const u32 len = static_cast<u32>(libk::strlen(dst_name));
        u32 dot = len;
        for (u32 i = len; i > 0; --i)
            if (dst_name[i - 1] == '.')
            {
                dot = i - 1;
                break;
            }

        char tmp[80];
        u32 tt = 0;
        for (u32 i = 0; i < dot && tt < 60; ++i)
            tmp[tt++] = dst_name[i];
        const char* suffix = "_copy";
        while (*suffix && tt < 63)
            tmp[tt++] = *suffix++;
        for (u32 i = dot; i < len && tt < 63; ++i)
            tmp[tt++] = dst_name[i];
        tmp[tt] = 0;
        for (u32 i = 0; i <= tt; ++i)
            dst_name[i] = tmp[i];
    }

    if (fs::vfs_create(dest_dir, dst_name) != 0)
    {
        mm::Heap::deallocate(bytes);
        log::write(log::Level::Warn, "clip", "paste: create '%s' failed", dst_name);
        return;
    }

    auto* dst_vn = fs::vnode_find_child(dest_dir, dst_name);
    if (!dst_vn || !dst_vn->ops || !dst_vn->ops->write)
    {
        mm::Heap::deallocate(bytes);
        return;
    }
    (void)dst_vn->ops->write(dst_vn, bytes, 0, static_cast<usize>(sz));

    if (c.cut && src_vn)
        (void)fs::vfs_unlink(src_vn);

    mm::Heap::deallocate(bytes);

    clipboard::clear();

    explorer_refresh();

    log::write(log::Level::Info, "clip", "paste: wrote '%s' (%lld bytes)%s", dst_name,
               static_cast<long long>(sz), c.cut ? " [cut]" : "");
}

} // namespace notyvos::gfx
