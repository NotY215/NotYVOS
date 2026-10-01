#pragma once
#include <kernel/types.hpp>

namespace notyvos::img::svg
{

struct Bitmap
{
    u32 width;
    u32 height;
    u32* pixels; // 0x00RRGGBB, zero-filled initially
    bool owned;
};

// Rasterize an SVG string into a `width` x `height` ARGB buffer.
// Supports a practical subset:
//   * Elements: <svg> <path> <rect> <circle> <polygon> <polyline> <line>
//   * Attributes: viewBox, d, points, x, y, width, height, cx, cy, r,
//     x1, y1, x2, y2, fill
//   * Path: M m L l H h V v C c Q q Z z
//   * Fill: #RGB, #RRGGBB, named colours, "none"
//   * Ignores: <g>, <defs>, gradients, strokes, transforms, CSS, arcs
bool rasterize(const char* svg_src, usize svg_len, u32 width, u32 height, Bitmap& out) noexcept;

void free(Bitmap& bmp) noexcept;

} // namespace notyvos::img::svg
