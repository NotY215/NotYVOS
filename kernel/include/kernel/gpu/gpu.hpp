#pragma once
#include <kernel/types.hpp>

namespace notyvos::gpu
{

// ---------------------------------------------------------------------------
// NOTYVOS GPU API
//
// A hardware-independent rendering API. The current backend is a software
// rasterizer drawing into the scene buffer. A native GPU driver replaces the
// backend without changing this header.
//
// Coordinates are in the current frame's surface space, top-left origin.
// Colors are 0x00RRGGBB.
// ---------------------------------------------------------------------------

struct Vertex
{
    float x, y;
    float u, v;
    u32 color;
};

struct Triangle
{
    Vertex v0, v1, v2;
};

// Bind the target surface. Called once per frame by the compositor.
void bind_surface(u32* pixels, u32 width, u32 height, u32 pitch) noexcept;

// Primitive pipeline.
void clear(u32 color) noexcept;
void draw_triangle(const Triangle& t) noexcept;
void draw_quad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3) noexcept;
void draw_line_f(i32 x0, i32 y0, i32 x1, i32 y1, u32 color) noexcept;

// Filled 2D rectangle -- direct path, no rasterizer.
void draw_rect(i32 x, i32 y, i32 w, i32 h, u32 color) noexcept;

// Backend info.
const char* backend_name() noexcept;
u64 frames_rendered() noexcept;

} // namespace notyvos::gpu
