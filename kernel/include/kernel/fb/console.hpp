#pragma once
#include <kernel/types.hpp>

namespace notyvos::fb
{

class Console
{
public:
    static void init() noexcept;
    static bool ready() noexcept;

    static void put(char c) noexcept;
    static void puts(const char* s) noexcept;

    static void clear() noexcept;

    // Switch to compositor-driven rendering. After this call, Console::put
    // routes through the compositor instead of drawing to the framebuffer.
    static void switch_to_buffered() noexcept;
    static bool is_buffered() noexcept;

    static void set_colors(u32 fg_argb, u32 bg_argb) noexcept;
    static u32 fg() noexcept;
    static u32 bg() noexcept;

    static u32 cols() noexcept;
    static u32 rows() noexcept;
    static u32 cursor_col() noexcept;
    static u32 cursor_row() noexcept;

    static void set_cursor(u32 col, u32 row) noexcept;

    // Force a redraw of the blinking-block cursor at the current position.
    // Called automatically after every put(); exposed for the shell to call
    // after a set_cursor().
    static void refresh_cursor() noexcept;

private:
    static void scroll_up_one() noexcept;
    static void advance_cursor() noexcept;
    static void newline() noexcept;
    static void backspace() noexcept;
    static void draw_glyph(u32 col, u32 row, char c) noexcept;
    static void erase_cursor() noexcept;
    static void draw_cursor() noexcept;
};

} // namespace notyvos::fb
