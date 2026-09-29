#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

// ---------------------------------------------------------------------------
// Graphics HAL
//
// Boundary between the Graphics API (api.hpp) and whatever backend owns
// pixels. A backend registers itself by implementing this struct and calling
// hal_register_backend().
//
// The software backend renders directly into the compositor's scene buffer.
// A GPU backend later will render to a texture and copy it into the scene,
// but the same HAL surface is used.
// ---------------------------------------------------------------------------

struct HalSurface
{
    u32* pixels; // 0x00RRGGBB
    u32 width;
    u32 height;
    u32 pitch; // in pixels
};

struct HalBackend
{
    const char* name;

    bool (*init)();
    void (*shutdown)();

    void (*begin_frame)(HalSurface* target);
    void (*end_frame)();

    void (*clear)(HalSurface*, u32 color);
    void (*pixel)(HalSurface*, i32 x, i32 y, u32 color);
    void (*fill_rect)(HalSurface*, i32 x, i32 y, i32 w, i32 h, u32 color);
    void (*hline)(HalSurface*, i32 x, i32 y, i32 w, u32 color);
    void (*vline)(HalSurface*, i32 x, i32 y, i32 h, u32 color);
    void (*circle)(HalSurface*, i32 cx, i32 cy, i32 r, u32 color);
};

bool hal_register_backend(const HalBackend* backend) noexcept;
const HalBackend* hal_current() noexcept;
HalSurface* hal_target() noexcept;

void hal_begin_frame(HalSurface* target) noexcept;
void hal_end_frame() noexcept;

void hal_clear(u32 color) noexcept;
void hal_pixel(i32 x, i32 y, u32 color) noexcept;
void hal_fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color) noexcept;
void hal_hline(i32 x, i32 y, i32 w, u32 color) noexcept;
void hal_vline(i32 x, i32 y, i32 h, u32 color) noexcept;
void hal_circle(i32 cx, i32 cy, i32 r, u32 color) noexcept;

namespace sw
{
void clear(HalSurface*, u32 color) noexcept;
void pixel(HalSurface*, i32 x, i32 y, u32 color) noexcept;
void fill_rect(HalSurface*, i32 x, i32 y, i32 w, i32 h, u32 color) noexcept;
void hline(HalSurface*, i32 x, i32 y, i32 w, u32 color) noexcept;
void vline(HalSurface*, i32 x, i32 y, i32 h, u32 color) noexcept;
void circle(HalSurface*, i32 cx, i32 cy, i32 r, u32 color) noexcept;
} // namespace sw

void register_software_backend() noexcept;

// Set the surface that the current frame draws into. Called by the
// compositor each time it starts a new frame.
void hal_set_target(u32* pixels, u32 width, u32 height, u32 pitch) noexcept;

} // namespace notyvos::gfx
