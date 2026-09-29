#include <kernel/gfx/hal.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::gfx
{

namespace
{

const HalBackend* g_backend = nullptr;
HalSurface* g_target = nullptr;

} // namespace

bool hal_register_backend(const HalBackend* b) noexcept
{
    if (!b || !b->name)
        return false;
    g_backend = b;
    log::write(log::Level::Info, "gfx-hal", "backend registered: %s", b->name);
    return true;
}

const HalBackend* hal_current() noexcept
{
    return g_backend;
}
HalSurface* hal_target() noexcept
{
    return g_target;
}

void hal_begin_frame(HalSurface* target) noexcept
{
    g_target = target;
    if (g_backend && g_backend->begin_frame)
        g_backend->begin_frame(target);
}

void hal_end_frame() noexcept
{
    if (g_backend && g_backend->end_frame)
        g_backend->end_frame();
    g_target = nullptr;
}

void hal_clear(u32 color) noexcept
{
    if (!g_target)
        return;
    if (g_backend && g_backend->clear)
        g_backend->clear(g_target, color);
    else
        sw::clear(g_target, color);
}

void hal_pixel(i32 x, i32 y, u32 color) noexcept
{
    if (!g_target)
        return;
    if (g_backend && g_backend->pixel)
        g_backend->pixel(g_target, x, y, color);
    else
        sw::pixel(g_target, x, y, color);
}

void hal_fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color) noexcept
{
    if (!g_target)
        return;
    if (g_backend && g_backend->fill_rect)
        g_backend->fill_rect(g_target, x, y, w, h, color);
    else
        sw::fill_rect(g_target, x, y, w, h, color);
}

void hal_hline(i32 x, i32 y, i32 w, u32 color) noexcept
{
    if (!g_target)
        return;
    if (g_backend && g_backend->hline)
        g_backend->hline(g_target, x, y, w, color);
    else
        sw::hline(g_target, x, y, w, color);
}

void hal_vline(i32 x, i32 y, i32 h, u32 color) noexcept
{
    if (!g_target)
        return;
    if (g_backend && g_backend->vline)
        g_backend->vline(g_target, x, y, h, color);
    else
        sw::vline(g_target, x, y, h, color);
}

void hal_circle(i32 cx, i32 cy, i32 r, u32 color) noexcept
{
    if (!g_target)
        return;
    if (g_backend && g_backend->circle)
        g_backend->circle(g_target, cx, cy, r, color);
    else
        sw::circle(g_target, cx, cy, r, color);
}

namespace sw
{

void clear(HalSurface* s, u32 color) noexcept
{
    if (!s || !s->pixels)
        return;
    for (u32 y = 0; y < s->height; ++y)
    {
        u32* row = s->pixels + static_cast<usize>(y) * s->pitch;
        for (u32 x = 0; x < s->width; ++x)
            row[x] = color;
    }
}

void pixel(HalSurface* s, i32 x, i32 y, u32 color) noexcept
{
    if (!s || !s->pixels)
        return;
    if (x < 0 || y < 0)
        return;
    if (static_cast<u32>(x) >= s->width)
        return;
    if (static_cast<u32>(y) >= s->height)
        return;
    s->pixels[static_cast<usize>(y) * s->pitch + static_cast<usize>(x)] = color;
}

void fill_rect(HalSurface* s, i32 x, i32 y, i32 w, i32 h, u32 color) noexcept
{
    if (!s || !s->pixels)
        return;
    if (w <= 0 || h <= 0)
        return;
    i32 x0 = x < 0 ? 0 : x;
    i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + w;
    if (x1 > static_cast<i32>(s->width))
        x1 = static_cast<i32>(s->width);
    i32 y1 = y + h;
    if (y1 > static_cast<i32>(s->height))
        y1 = static_cast<i32>(s->height);
    if (x0 >= x1 || y0 >= y1)
        return;
    for (i32 j = y0; j < y1; ++j)
    {
        u32* row = s->pixels + static_cast<usize>(j) * s->pitch;
        for (i32 i = x0; i < x1; ++i)
            row[i] = color;
    }
}

void hline(HalSurface* s, i32 x, i32 y, i32 w, u32 color) noexcept
{
    fill_rect(s, x, y, w, 1, color);
}

void vline(HalSurface* s, i32 x, i32 y, i32 h, u32 color) noexcept
{
    fill_rect(s, x, y, 1, h, color);
}

void circle(HalSurface* s, i32 cx, i32 cy, i32 r, u32 color) noexcept
{
    if (r <= 0)
        return;
    const i32 r2 = r * r;
    for (i32 dy = -r; dy <= r; ++dy)
    {
        i32 x_span = 0;
        while ((x_span * x_span) + (dy * dy) <= r2)
            ++x_span;
        --x_span;
        if (x_span < 0)
            continue;
        fill_rect(s, cx - x_span, cy + dy, x_span * 2 + 1, 1, color);
    }
}

} // namespace sw

} // namespace notyvos::gfx
