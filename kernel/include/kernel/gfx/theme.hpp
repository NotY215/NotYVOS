#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx::theme
{

enum class Id : u8
{
    Dark = 0,
    Light = 1,
    MacDark = 2
};

struct Palette
{
    const char* name;
    u32 desktop_top, desktop_bottom;
    u32 taskbar_bg, taskbar_hi;
    u32 title_focused, title_unfocused;
    u32 title_fg_focused, title_fg_unfocused;
    u32 border_focused, border_unfocused;
    u32 client_bg, text_fg, text_dim;
    u32 accent;
    u32 menu_bg, menu_hi, menu_fg, menu_sep;
    u32 btn_close, btn_hover;
    u32 shortcut_bg, shortcut_hover, shortcut_focus;
    u32 cursor_fg, cursor_shadow;
};

const Palette& current() noexcept;
Id current_id() noexcept;
void set(Id id) noexcept;
const Palette& get(Id id) noexcept;
u32 count() noexcept;

} // namespace notyvos::gfx::theme
