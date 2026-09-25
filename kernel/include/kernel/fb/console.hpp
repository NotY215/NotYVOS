#pragma once

#include <kernel/types.hpp>

namespace notyvos::fb
{

class Console
{
public:
    // Must be called after Framebuffer::init(). Idempotent.
    static void init() noexcept;

    static bool ready() noexcept;

    static void put(char c) noexcept;
    static void puts(const char* s) noexcept;

    static void clear() noexcept;

    static void set_colors(u32 fg_argb, u32 bg_argb) noexcept;
    static u32 fg() noexcept;
    static u32 bg() noexcept;

    static u32 cols() noexcept;
    static u32 rows() noexcept;
    static u32 cursor_col() noexcept;
    static u32 cursor_row() noexcept;

    static void set_cursor(u32 col, u32 row) noexcept;

private:
    static void scroll_up_one() noexcept;
    static void advance_cursor() noexcept;
    static void newline() noexcept;
    static void backspace() noexcept;
    static void draw_glyph(u32 col, u32 row, char c) noexcept;
};

} // namespace notyvos::fb
