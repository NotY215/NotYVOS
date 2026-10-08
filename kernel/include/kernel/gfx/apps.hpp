#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

using AppRectFn = void (*)(i32 x, i32 y, i32 w, i32 h, u32 color);
using AppTextFn = void (*)(i32 x, i32 y, const char* s, u32 fg, u32 bg);

void apps_bind_impl(AppRectFn rect_fn, AppTextFn text_fn) noexcept;

// gx/gy = client-area origin (top-left inside the title bar)
// gw/gh = client-area dimensions
// mx/my = absolute mouse position
// mouse_down = left button currently held
// after apps_click_bin:
bool apps_draw_imageviewer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my,
                           bool mouse_down) noexcept;
bool apps_draw_gamelauncher(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my,
                            bool mouse_down) noexcept;
void apps_load_image(const char* path) noexcept;
bool apps_draw_explorer(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept;
bool apps_draw_settings(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept;
bool apps_draw_bin(i32 gx, i32 gy, i32 gw, i32 gh, i32 mx, i32 my, bool mouse_down) noexcept;
void apps_explorer_nav_back() noexcept;
void apps_explorer_nav_forward() noexcept;
void apps_explorer_nav_up() noexcept;
void apps_explorer_nav_refresh() noexcept;
void apps_explorer_clipboard_copy(bool cut) noexcept;
void apps_explorer_clipboard_paste() noexcept;

void apps_explorer_right_click(i32 mx, i32 my) noexcept;
bool apps_explorer_click_ctx(i32 mx, i32 my) noexcept;

// Modal prompt used by Explorer for New Folder / Rename.
// Blocks until the user confirms (Enter) or cancels (Esc).
// Returns true on confirm; the entered text is written to `out`.
// `out` must be at least `cap` bytes; `cap` should be <= 63.
bool apps_prompt_text(const char* title, const char* initial, char* out, usize cap) noexcept;

// Modal confirmation used by Explorer for Delete.
bool apps_prompt_confirm(const char* title, const char* message) noexcept;
bool apps_click_explorer(i32 mx, i32 my, bool pressed_edge) noexcept;
bool apps_click_settings(i32 mx, i32 my, bool pressed_edge) noexcept;
bool apps_click_bin(i32 mx, i32 my, bool pressed_edge) noexcept;
// Wi-Fi UI helpers used by the compositor tick.
void wifi_pump_scan() noexcept;
void wifi_start_scan() noexcept;

} // namespace notyvos::gfx
