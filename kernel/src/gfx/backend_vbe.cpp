#include <kernel/fb/framebuffer.hpp>
#include <kernel/gfx/backend_vbe.hpp>
#include <kernel/gfx/hal.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::gfx
{

namespace
{

u8* g_vram = nullptr; // HHDM-mapped framebuffer base
u32 g_fb_w = 0;
u32 g_fb_h = 0;
u32 g_fb_pitch = 0;
u32 g_fb_bpp_bytes = 4;
u32 g_gpu_count = 0;

inline void outl_(u16 p, u32 v)
{
    asm volatile("outl %0, %1" ::"a"(v), "Nd"(p));
}
inline u32 inl_(u16 p)
{
    u32 v;
    asm volatile("inl %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 off)
{
    const u32 addr = (1u << 31) | (static_cast<u32>(bus) << 16) | (static_cast<u32>(slot) << 11) |
                     (static_cast<u32>(func) << 8) | (off & 0xFC);
    outl_(0xCF8, addr);
    return inl_(0xCFC);
}

void enumerate_gpus() noexcept
{
    for (u32 bus = 0; bus < 8; ++bus)
    {
        for (u32 slot = 0; slot < 32; ++slot)
        {
            const u32 id = pci_read32(static_cast<u8>(bus), static_cast<u8>(slot), 0, 0);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            const u32 classrev = pci_read32(static_cast<u8>(bus), static_cast<u8>(slot), 0, 8);
            const u8 cls = static_cast<u8>((classrev >> 24) & 0xFF);
            if (cls != 0x03)
                continue; // Display controller

            const u16 vendor = static_cast<u16>(id & 0xFFFF);
            const u16 device = static_cast<u16>((id >> 16) & 0xFFFF);
            log::write(log::Level::Info, "gfx-gpu",
                       "display controller PCI %u:%u vendor=0x%llx device=0x%llx",
                       static_cast<unsigned long long>(bus), static_cast<unsigned long long>(slot),
                       static_cast<unsigned long long>(vendor),
                       static_cast<unsigned long long>(device));
            ++g_gpu_count;
        }
    }
}

// ---- Blit implementations ----

void blit_argb32(const u32* src, u32 src_pitch, u32 dx, u32 dy, u32 w, u32 h) noexcept
{
    for (u32 j = 0; j < h; ++j)
    {
        const u32* s = src + static_cast<usize>(j) * src_pitch;
        u8* d = g_vram + static_cast<usize>(dy + j) * g_fb_pitch + static_cast<usize>(dx) * 4;
        libk::memcpy(d, s, static_cast<usize>(w) * 4);
    }
}

void blit_rgb24(const u32* src, u32 src_pitch, u32 dx, u32 dy, u32 w, u32 h) noexcept
{
    for (u32 j = 0; j < h; ++j)
    {
        const u32* s = src + static_cast<usize>(j) * src_pitch;
        u8* d = g_vram + static_cast<usize>(dy + j) * g_fb_pitch + static_cast<usize>(dx) * 3;
        for (u32 i = 0; i < w; ++i)
        {
            const u32 c = s[i];
            d[i * 3 + 0] = static_cast<u8>(c & 0xFF);
            d[i * 3 + 1] = static_cast<u8>((c >> 8) & 0xFF);
            d[i * 3 + 2] = static_cast<u8>((c >> 16) & 0xFF);
        }
    }
}

void blit_rgb16(const u32* src, u32 src_pitch, u32 dx, u32 dy, u32 w, u32 h) noexcept
{
    for (u32 j = 0; j < h; ++j)
    {
        const u32* s = src + static_cast<usize>(j) * src_pitch;
        u16* d = reinterpret_cast<u16*>(g_vram + static_cast<usize>(dy + j) * g_fb_pitch +
                                        static_cast<usize>(dx) * 2);
        for (u32 i = 0; i < w; ++i)
        {
            d[i] = static_cast<u16>(s[i] & 0xFFFF);
        }
    }
}

} // namespace

u32 vbe_gpu_count() noexcept
{
    return g_gpu_count;
}

void vbe_blit(const u32* src, u32 src_w, u32 src_h, u32 src_pitch, u32 dst_x, u32 dst_y, u32 copy_w,
              u32 copy_h) noexcept
{
    if (!g_vram || !src)
        return;
    if (src_w == 0 || src_h == 0)
        return;
    if (dst_x >= g_fb_w || dst_y >= g_fb_h)
        return;

    u32 w = copy_w;
    u32 h = copy_h;
    if (dst_x + w > g_fb_w)
        w = g_fb_w - dst_x;
    if (dst_y + h > g_fb_h)
        h = g_fb_h - dst_y;
    if (w == 0 || h == 0)
        return;

    switch (g_fb_bpp_bytes)
    {
    case 4:
        blit_argb32(src, src_pitch, dst_x, dst_y, w, h);
        break;
    case 3:
        blit_rgb24(src, src_pitch, dst_x, dst_y, w, h);
        break;
    case 2:
        blit_rgb16(src, src_pitch, dst_x, dst_y, w, h);
        break;
    default:
        break;
    }
}

// ---- HAL backend entry points ----

namespace
{

bool vbe_init() noexcept
{
    if (!fb::Framebuffer::ready())
    {
        log::write(log::Level::Warn, "gfx-gpu", "no framebuffer; VBE unavailable");
        return false;
    }
    g_vram = static_cast<u8*>(fb::Framebuffer::data());
    g_fb_w = fb::Framebuffer::width();
    g_fb_h = fb::Framebuffer::height();
    g_fb_pitch = fb::Framebuffer::pitch();
    g_fb_bpp_bytes = fb::Framebuffer::bytes_per_pixel();

    log::write(log::Level::Info, "gfx-gpu", "VRAM %llu x %llu pitch %llu bpp %llu",
               static_cast<unsigned long long>(g_fb_w), static_cast<unsigned long long>(g_fb_h),
               static_cast<unsigned long long>(g_fb_pitch),
               static_cast<unsigned long long>(g_fb_bpp_bytes));

    enumerate_gpus();
    return true;
}

void vbe_shutdown() noexcept {}
void vbe_begin_frame(HalSurface*) noexcept {}
void vbe_end_frame() noexcept {}

// The HAL calls here target the intermediate scene buffer, not the screen.
// Real screen blitting happens via vbe_blit, driven by the compositor.

// Fill, pixel, line, circle all defer to the software path since they
// target the scene buffer.
void vbe_fill_rect(HalSurface* s, i32 x, i32 y, i32 w, i32 h, u32 c) noexcept
{
    sw::fill_rect(s, x, y, w, h, c);
}
void vbe_pixel(HalSurface* s, i32 x, i32 y, u32 c) noexcept
{
    sw::pixel(s, x, y, c);
}
void vbe_clear(HalSurface* s, u32 c) noexcept
{
    sw::clear(s, c);
}
void vbe_hline(HalSurface* s, i32 x, i32 y, i32 w, u32 c) noexcept
{
    sw::hline(s, x, y, w, c);
}
void vbe_vline(HalSurface* s, i32 x, i32 y, i32 h, u32 c) noexcept
{
    sw::vline(s, x, y, h, c);
}
void vbe_circle(HalSurface* s, i32 cx, i32 cy, i32 r, u32 c) noexcept
{
    sw::circle(s, cx, cy, r, c);
}

const HalBackend g_vbe_backend = {
    "vbe",     vbe_init,      vbe_shutdown, vbe_begin_frame, vbe_end_frame, vbe_clear,
    vbe_pixel, vbe_fill_rect, vbe_hline,    vbe_vline,       vbe_circle,
};

} // namespace

bool vbe_backend_init() noexcept
{
    if (!g_vbe_backend.init())
        return false;
    hal_register_backend(&g_vbe_backend);
    log::write(log::Level::Info, "gfx-gpu", "VBE backend registered, %llu GPU(s) found",
               static_cast<unsigned long long>(g_gpu_count));
    return true;
}

} // namespace notyvos::gfx
