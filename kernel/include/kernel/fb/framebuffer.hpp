#pragma once
#include <kernel/types.hpp>
#include <limine.h>

namespace notyvos::fb
{

class Framebuffer
{
public:
    static void init(limine_framebuffer* fb) noexcept;
    static bool ready() noexcept;

    static u32 width() noexcept;
    static u32 height() noexcept;
    static u32 pitch() noexcept;
    static u16 bpp() noexcept;
    static u32 bytes_per_pixel() noexcept;

    // Raw pointer to the top-left pixel. Valid as long as the framebuffer is.
    static void* data() noexcept;

    static void put_pixel(u32 x, u32 y, u32 argb) noexcept;
    static void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 argb) noexcept;
    static void clear(u32 argb) noexcept;

    // Copy a horizontal strip: dst_y <- src_y, rows_pixels wide.
    static void copy_strip(u32 src_y, u32 dst_y, u32 height_px) noexcept;

    static void blit_glyph_8x8(u32 x, u32 y, u32 scale, const u8 rows[8], u32 fg, u32 bg) noexcept;
};

} // namespace notyvos::fb
