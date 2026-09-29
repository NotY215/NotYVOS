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

// ---------- Explorer ----------
struct ExplorerState
{
    bool initialized;
    ListView files;
    Button open_btn;
    char file_names[16][64];
    const char* file_ptrs[16];
    u32 file_count;
};
ExplorerState g_exp{};

void explorer_collect()
{
    g_exp.file_count = 0;
    auto* root = fs::vfs_root();
    if (!root)
        return;
    for (fs::VNode* c = root->children; c && g_exp.file_count < 16u; c = c->next)
    {
        u32 i = 0;
        while (c->name[i] && i < 63)
        {
            g_exp.file_names[g_exp.file_count][i] = c->name[i];
            ++i;
        }
        g_exp.file_names[g_exp.file_count][i] = 0;
        g_exp.file_ptrs[g_exp.file_count] = g_exp.file_names[g_exp.file_count];
        ++g_exp.file_count;
    }
}

void explorer_init(i32 gx, i32 gy, i32 gw, i32 gh)
{
    if (g_exp.initialized)
        return;
    Rect area = {gx + 8, gy + 8, gw - 16, gh - 48};
    g_exp.files.init(area);
    explorer_collect();
    for (u32 i = 0; i < g_exp.file_count; ++i)
    {
        g_exp.files.add(g_exp.file_ptrs[i]);
    }
    g_exp.open_btn.init("Open", {gx + 8, gy + gh - 34, 100, 26}, nullptr, nullptr);
    g_exp.initialized = true;
}

// ---------- Settings ----------
void settings_shutdown_cb(void*)
{
    acpi::power_off();
}
void settings_restart_cb(void*)
{
    acpi::restart();
}

struct SettingsState
{
    bool initialized;
    Label title, line1, line2, line3;
    Button shutdown_btn, restart_btn;
};
SettingsState g_set{};

void settings_init(i32 gx, i32 gy)
{
    if (g_set.initialized)
        return;
    g_set.title.init("System Settings", gx + 16, gy + 12);
    g_set.line1.init("Display: 800x600", gx + 16, gy + 44);
    g_set.line2.init("Storage: NYFS on SATA", gx + 16, gy + 60);
    g_set.line3.init("Input: PS/2 kb + mouse", gx + 16, gy + 76);
    g_set.shutdown_btn.init("Shut down", {gx + 16, gy + 130, 140, 30}, settings_shutdown_cb,
                            nullptr);
    g_set.restart_btn.init("Restart", {gx + 170, gy + 130, 140, 30}, settings_restart_cb, nullptr);
    g_set.initialized = true;
}

// ---------- Bin ----------
struct BinState
{
    bool initialized;
    Label message;
};
BinState g_bin{};

void bin_init(i32 gx, i32 gy)
{
    if (g_bin.initialized)
        return;
    g_bin.message.init("Recycle Bin is empty.", gx + 16, gy + 24);
    g_bin.initialized = true;
}

} // namespace

void apps_bind_impl(AppRectFn rect_fn, AppTextFn text_fn) noexcept
{
    widget_bind(rect_fn, text_fn);
}

bool apps_draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept
{
    explorer_init(gx, gy, gw, gh);
    g_exp.files.draw(mx, my);
    g_exp.open_btn.draw(mx, my, mouse_down);
    return true;
}

bool apps_draw_settings(i32 gx, i32 gy, i32, i32, i32 mx, i32 my, bool mouse_down) noexcept
{
    settings_init(gx, gy);
    g_set.title.draw();
    g_set.line1.draw();
    g_set.line2.draw();
    g_set.line3.draw();
    g_set.shutdown_btn.draw(mx, my, mouse_down);
    g_set.restart_btn.draw(mx, my, mouse_down);
    return true;
}

bool apps_draw_bin(i32 gx, i32 gy, i32, i32, i32, i32, bool) noexcept
{
    bin_init(gx, gy);
    g_bin.message.draw();
    return true;
}

bool apps_click_explorer(i32 mx, i32 my, bool pressed_edge) noexcept
{
    (void)g_exp.files.on_click(mx, my, pressed_edge);
    (void)g_exp.open_btn.on_click(mx, my, pressed_edge);
    return true;
}

bool apps_click_settings(i32 mx, i32 my, bool pressed_edge) noexcept
{
    (void)g_set.shutdown_btn.on_click(mx, my, pressed_edge);
    (void)g_set.restart_btn.on_click(mx, my, pressed_edge);
    return true;
}

bool apps_click_bin(i32, i32, bool) noexcept
{
    return true;
}

} // namespace notyvos::gfx
