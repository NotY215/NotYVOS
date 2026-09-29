#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

// ---------------------------------------------------------------------------
// NOTYVOS Graphics API
//
// Device-independent drawing interface. Every backend (software today, GPU
// later) must implement the same surface. Applications do not talk to
// backends directly; they call these functions.
//
// Conventions:
//   - Colors are 0x00RRGGBB
//   - Coordinates are top-left origin, X right, Y down
//   - All calls are synchronous
//   - The HAL target is the compositor's scene buffer
// ---------------------------------------------------------------------------

using Color = u32;

// Device handle. Currently there is one global device (the compositor's
// scene). Future hardware backends will return distinct handles.
class Device
{
public:
    static void init() noexcept;
    static void shutdown() noexcept;

    static void begin_frame() noexcept;
    static void end_frame() noexcept;

    static u32 width() noexcept;
    static u32 height() noexcept;
};

// Immediate-mode drawing. These are the only primitives. Everything else
// (windows, buttons, lines, polygons) is built on top.
namespace draw
{

void clear(Color c) noexcept;

void pixel(i32 x, i32 y, Color c) noexcept;

void fill_rect(i32 x, i32 y, i32 w, i32 h, Color c) noexcept;
void rect(i32 x, i32 y, i32 w, i32 h, Color c) noexcept;

void hline(i32 x, i32 y, i32 w, Color c) noexcept;
void vline(i32 x, i32 y, i32 h, Color c) noexcept;

void line(i32 x0, i32 y0, i32 x1, i32 y1, Color c) noexcept;

// Filled axis-aligned rectangle with a per-side border color.
void panel(i32 x, i32 y, i32 w, i32 h, Color fill, Color border) noexcept;

// Circle approximation using horizontal spans.
void circle(i32 cx, i32 cy, i32 r, Color c) noexcept;

void text(i32 x, i32 y, const char* s, Color fg, Color bg) noexcept;

} // namespace draw

} // namespace notyvos::gfx
