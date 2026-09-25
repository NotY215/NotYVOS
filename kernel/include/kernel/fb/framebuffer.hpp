#pragma once

#include <kernel/types.hpp>
#include <limine.h>

namespace notyvos::fb {

class Framebuffer {
public:
    static void init(limine_framebuffer* fb) noexcept;

    static u32 width()  noexcept;
    static u32 height() noexcept;
    static u32 pitch()  noexcept;
    static u16 bpp()    noexcept;

    static void clear(u32 argb) noexcept;
    static void put_pixel(u32 x, u32 y, u32 argb) noexcept;

    // Draw a NUL-terminated string using the embedded 8x8 font.
    // Coordinates are in characters.
    static void draw_text(u32 col, u32 row, const char* s, u32 fg, u32 bg) noexcept;

    static void draw_char(u32 col, u32 row, char c, u32 fg, u32 bg) noexcept;

    static bool ready() noexcept;
};

} // namespace notyvos::fb