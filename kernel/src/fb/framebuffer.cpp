#include <kernel/fb/framebuffer.hpp>
#include <kernel/libk/mem.hpp>

namespace notyvos::fb
{

namespace
{
limine_framebuffer* g_fb = nullptr;

inline u32 bpp_bytes() noexcept
{
    return g_fb ? ((g_fb->bpp + 7u) / 8u) : 4u;
}

inline void store_pixel(u32 x, u32 y, u32 argb)
{
    if (!g_fb)
        return;
    if (x >= g_fb->width || y >= g_fb->height)
        return;
    auto* base = static_cast<u8*>(g_fb->address);
    const u32 bp = bpp_bytes();
    u8* p = base + static_cast<usize>(y) * g_fb->pitch + static_cast<usize>(x) * bp;
    switch (bp)
    {
    case 4:
        *reinterpret_cast<u32*>(p) = argb;
        break;
    case 3:
        p[0] = static_cast<u8>(argb & 0xFF);
        p[1] = static_cast<u8>((argb >> 8) & 0xFF);
        p[2] = static_cast<u8>((argb >> 16) & 0xFF);
        break;
    case 2:
        *reinterpret_cast<u16*>(p) = static_cast<u16>(argb & 0xFFFF);
        break;
    default:
        break;
    }
}
} // namespace

void Framebuffer::init(limine_framebuffer* fb) noexcept
{
    g_fb = fb;
}
bool Framebuffer::ready() noexcept
{
    return g_fb != nullptr;
}
u32 Framebuffer::width() noexcept
{
    return g_fb ? static_cast<u32>(g_fb->width) : 0;
}
u32 Framebuffer::height() noexcept
{
    return g_fb ? static_cast<u32>(g_fb->height) : 0;
}
u32 Framebuffer::pitch() noexcept
{
    return g_fb ? static_cast<u32>(g_fb->pitch) : 0;
}
u16 Framebuffer::bpp() noexcept
{
    return g_fb ? g_fb->bpp : 0;
}
u32 Framebuffer::bytes_per_pixel() noexcept
{
    return bpp_bytes();
}
void* Framebuffer::data() noexcept
{
    return g_fb ? g_fb->address : nullptr;
}

void Framebuffer::put_pixel(u32 x, u32 y, u32 argb) noexcept
{
    store_pixel(x, y, argb);
}

void Framebuffer::fill_rect(u32 x, u32 y, u32 w, u32 h, u32 argb) noexcept
{
    if (!g_fb)
        return;
    const u32 x1 = (x + w > width()) ? width() : x + w;
    const u32 y1 = (y + h > height()) ? height() : y + h;
    for (u32 j = y; j < y1; ++j)
        for (u32 i = x; i < x1; ++i)
            store_pixel(i, j, argb);
}

void Framebuffer::clear(u32 argb) noexcept
{
    fill_rect(0, 0, width(), height(), argb);
}

void Framebuffer::copy_strip(u32 src_y, u32 dst_y, u32 rows_px) noexcept
{
    if (!g_fb || src_y == dst_y || rows_px == 0)
        return;
    auto* base = static_cast<u8*>(g_fb->address);
    const usize row_bytes = static_cast<usize>(g_fb->pitch);
    const u8* src = base + static_cast<usize>(src_y) * row_bytes;
    u8* dst = base + static_cast<usize>(dst_y) * row_bytes;
    libk::memmove(dst, src, row_bytes * rows_px);
}

void Framebuffer::blit_glyph_8x8(u32 x, u32 y, u32 scale, const u8 rows[8], u32 fg, u32 bg) noexcept
{
    if (!g_fb)
        return;
    for (u32 gy = 0; gy < 8; ++gy)
    {
        const u8 bits = rows[gy];
        for (u32 gx = 0; gx < 8; ++gx)
        {
            const bool on = (bits >> (7 - gx)) & 1u;
            const u32 color = on ? fg : bg;
            for (u32 sy = 0; sy < scale; ++sy)
                for (u32 sx = 0; sx < scale; ++sx)
                    store_pixel(x + gx * scale + sx, y + gy * scale + sy, color);
        }
    }
}

} // namespace notyvos::fb
