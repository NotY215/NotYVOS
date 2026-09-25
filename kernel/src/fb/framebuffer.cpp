#include <kernel/fb/framebuffer.hpp>
#include <kernel/arch/x86_64/serial.hpp>
#include "font8x8.hpp"

namespace notyvos::fb {

namespace {
limine_framebuffer* g_fb = nullptr;
constexpr u32 kCharW = 8;
constexpr u32 kCharH = 8;
constexpr u32 kScale = 2;  // 16x16 on screen for readability

inline void store_pixel(u32 x, u32 y, u32 argb) {
    if (!g_fb) return;
    if (x >= g_fb->width || y >= g_fb->height) return;

    auto* base = static_cast<u8*>(g_fb->address);
    const u32 bpp_bytes = (g_fb->bpp + 7u) / 8u;
    u8* p = base + static_cast<usize>(y) * g_fb->pitch
                 + static_cast<usize>(x) * bpp_bytes;

    // Limine reports framebuffer in RGB (red_mask at 16, green at 8, blue at 0
    // for typical 32bpp). We pass through 0x00RRGGBB.
    switch (bpp_bytes) {
        case 4: *reinterpret_cast<u32*>(p) = argb; break;
        case 3:
            p[0] = static_cast<u8>(argb & 0xFF);
            p[1] = static_cast<u8>((argb >> 8) & 0xFF);
            p[2] = static_cast<u8>((argb >> 16) & 0xFF);
            break;
        case 2: *reinterpret_cast<u16*>(p) = static_cast<u16>(argb & 0xFFFF); break;
        default: break;
    }
}
} // namespace

void Framebuffer::init(limine_framebuffer* fb) noexcept { g_fb = fb; }
u32 Framebuffer::width()  noexcept { return g_fb ? static_cast<u32>(g_fb->width)  : 0; }
u32 Framebuffer::height() noexcept { return g_fb ? static_cast<u32>(g_fb->height) : 0; }
u32 Framebuffer::pitch()  noexcept { return g_fb ? static_cast<u32>(g_fb->pitch)  : 0; }
u16 Framebuffer::bpp()    noexcept { return g_fb ? g_fb->bpp : 0; }
bool Framebuffer::ready() noexcept { return g_fb != nullptr; }

void Framebuffer::put_pixel(u32 x, u32 y, u32 argb) noexcept {
    store_pixel(x, y, argb);
}

void Framebuffer::clear(u32 argb) noexcept {
    if (!g_fb) return;
    for (u32 y = 0; y < height(); ++y)
        for (u32 x = 0; x < width(); ++x)
            store_pixel(x, y, argb);
}

void Framebuffer::draw_char(u32 col, u32 row, char c, u32 fg, u32 bg) noexcept {
    if (!g_fb) return;
    if (c < 0x20 || c > 0x7E) c = '?';
    const u8* glyph = kFont8x8[static_cast<int>(c) - 0x20];

    const u32 px = col * kCharW * kScale;
    const u32 py = row * kCharH * kScale;

    for (u32 gy = 0; gy < kCharH; ++gy) {
        const u8 bits = glyph[gy];
        for (u32 gx = 0; gx < kCharW; ++gx) {
            const bool on = (bits >> (7 - gx)) & 1u;
            const u32 color = on ? fg : bg;
            for (u32 sy = 0; sy < kScale; ++sy)
                for (u32 sx = 0; sx < kScale; ++sx)
                    store_pixel(px + gx * kScale + sx, py + gy * kScale + sy, color);
        }
    }
}

void Framebuffer::draw_text(u32 col, u32 row, const char* s, u32 fg, u32 bg) noexcept {
    if (!g_fb || !s) return;
    u32 c = col;
    for (; *s; ++s, ++c) draw_char(c, row, *s, fg, bg);
}

} // namespace notyvos::fb