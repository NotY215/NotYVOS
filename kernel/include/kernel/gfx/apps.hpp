#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

using AppRectFn = void (*)(i32 x, i32 y, i32 w, i32 h, u32 color);
using AppTextFn = void (*)(i32 x, i32 y, const char* s, u32 fg, u32 bg);

// Called once by Compositor::init to install widget primitives.
void apps_bind_impl(AppRectFn rect_fn, AppTextFn text_fn) noexcept;

bool apps_draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept;
bool apps_draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept;
bool apps_draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept;

// Click handlers. `mx`, `my` are absolute screen coordinates.
bool apps_click_explorer(i32 mx, i32 my, bool pressed_edge) noexcept;
bool apps_click_settings(i32 mx, i32 my, bool pressed_edge) noexcept;
bool apps_click_bin(i32 mx, i32 my, bool pressed_edge) noexcept;

} // namespace notyvos::gfx
