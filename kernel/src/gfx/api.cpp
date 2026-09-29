#include "../fb/font8x8.hpp"
#include <kernel/gfx/api.hpp>
#include <kernel/gfx/hal.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

namespace notyvos::gfx
{

namespace
{
HalSurface g_surface = {};
bool g_in_frame = false;
} // namespace

// The HAL surface points at whatever the compositor provides. This file
// does not own the pixels; it borrows them per frame.

void hal_set_target(u32* pixels, u32 width, u32 height, u32 pitch) noexcept
{
    g_surface.pixels = pixels;
    g_surface.width = width;
    g_surface.height = height;
    g_surface.pitch = pitch;
}

void Device::init() noexcept
{
    log::write(log::Level::Info, "gfx-api", "device initialized");
}

void Device::shutdown() noexcept
{
    log::write(log::Level::Info, "gfx-api", "device shut down");
}

void Device::begin_frame() noexcept
{
    hal_begin_frame(&g_surface);
    g_in_frame = true;
}

void Device::end_frame() noexcept
{
    hal_end_frame();
    g_in_frame = false;
}

u32 Device::width() noexcept
{
    return g_surface.width;
}
u32 Device::height() noexcept
{
    return g_surface.height;
}

namespace draw
{

void clear(Color c) noexcept
{
    hal_clear(c);
}

void pixel(i32 x, i32 y, Color c) noexcept
{
    hal_pixel(x, y, c);
}

void fill_rect(i32 x, i32 y, i32 w, i32 h, Color c) noexcept
{
    hal_fill_rect(x, y, w, h, c);
}

void rect(i32 x, i32 y, i32 w, i32 h, Color c) noexcept
{
    hal_hline(x, y, w, c);
    hal_hline(x, y + h - 1, w, c);
    hal_vline(x, y, h, c);
    hal_vline(x + w - 1, y, h, c);
}

void hline(i32 x, i32 y, i32 w, Color c) noexcept
{
    hal_hline(x, y, w, c);
}
void vline(i32 x, i32 y, i32 h, Color c) noexcept
{
    hal_vline(x, y, h, c);
}

void line(i32 x0, i32 y0, i32 x1, i32 y1, Color c) noexcept
{
    // Bresenham.
    i32 dx = x1 - x0;
    i32 dy = y1 - y0;
    const i32 sx = dx < 0 ? -1 : 1;
    const i32 sy = dy < 0 ? -1 : 1;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    i32 err = (dx > dy ? dx : -dy) / 2;
    for (;;)
    {
        hal_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        const i32 e2 = err;
        if (e2 > -dx)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void panel(i32 x, i32 y, i32 w, i32 h, Color fill, Color border) noexcept
{
    hal_fill_rect(x, y, w, h, fill);
    hal_hline(x, y, w, border);
    hal_hline(x, y + h - 1, w, border);
    hal_vline(x, y, h, border);
    hal_vline(x + w - 1, y, h, border);
}

void circle(i32 cx, i32 cy, i32 r, Color c) noexcept
{
    hal_circle(cx, cy, r, c);
}

void text(i32 x, i32 y, const char* s, Color fg, Color bg) noexcept
{
    if (!s)
        return;
    i32 px = x;
    while (*s)
    {
        char ch = *s++;
        if (ch < 0x20 || ch > 0x7E)
            ch = '?';
        const u8* rows = notyvos::fb::kFont8x8[static_cast<int>(ch) - 0x20];
        for (u32 gy = 0; gy < 8; ++gy)
        {
            const u8 bits = rows[gy];
            for (u32 gx = 0; gx < 8; ++gx)
            {
                const bool on = ((bits >> (7 - gx)) & 1u) != 0;
                const Color c = on ? fg : bg;
                hal_pixel(px + static_cast<i32>(gx), y + static_cast<i32>(gy), c);
            }
        }
        px += 8;
    }
}

} // namespace draw

} // namespace notyvos::gfx
