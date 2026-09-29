#include <kernel/gpu/gpu.hpp>
#include <kernel/libk/mem.hpp>

namespace notyvos::gpu {

namespace {

u32* g_pixels = nullptr;
u32  g_w = 0;
u32  g_h = 0;
u32  g_pitch = 0;
u64  g_frames = 0;

inline void put(i32 x, i32 y, u32 color) noexcept {
    if (!g_pixels) return;
    if (x < 0 || y < 0) return;
    if (static_cast<u32>(x) >= g_w) return;
    if (static_cast<u32>(y) >= g_h) return;
    g_pixels[static_cast<usize>(y) * g_pitch + static_cast<usize>(x)] = color;
}

inline i32 edge_fn(const Vertex& a, const Vertex& b, float px, float py) noexcept {
    return static_cast<i32>((px - a.x) * (b.y - a.y)
                          - (py - a.y) * (b.x - a.x));
}

} // namespace

void bind_surface(u32* pixels, u32 width, u32 height, u32 pitch) noexcept {
    g_pixels = pixels;
    g_w = width;
    g_h = height;
    g_pitch = pitch;
}

void clear(u32 color) noexcept {
    if (!g_pixels) return;
    for (u32 y = 0; y < g_h; ++y) {
        u32* row = g_pixels + static_cast<usize>(y) * g_pitch;
        for (u32 x = 0; x < g_w; ++x) row[x] = color;
    }
}

void draw_rect(i32 x, i32 y, i32 w, i32 h, u32 color) noexcept {
    if (!g_pixels) return;
    if (w <= 0 || h <= 0) return;
    const i32 x0 = x < 0 ? 0 : x;
    const i32 y0 = y < 0 ? 0 : y;
    i32 x1 = x + w;
    if (x1 > static_cast<i32>(g_w)) x1 = static_cast<i32>(g_w);
    i32 y1 = y + h;
    if (y1 > static_cast<i32>(g_h)) y1 = static_cast<i32>(g_h);
    if (x0 >= x1 || y0 >= y1) return;
    for (i32 j = y0; j < y1; ++j) {
        u32* row = g_pixels + static_cast<usize>(j) * g_pitch;
        for (i32 i = x0; i < x1; ++i) row[i] = color;
    }
}

void draw_line_f(i32 x0, i32 y0, i32 x1, i32 y1, u32 color) noexcept {
    i32 dx = x1 - x0;
    i32 dy = y1 - y0;
    const i32 sx = dx < 0 ? -1 : 1;
    const i32 sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    i32 err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        put(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const i32 e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

void draw_triangle(const Triangle& t) noexcept {
    if (!g_pixels) return;

    // Bounding box, computed from all three vertices without an
    // initializer list.
    i32 min_x = static_cast<i32>(t.v0.x);
    i32 max_x = min_x;
    i32 min_y = static_cast<i32>(t.v0.y);
    i32 max_y = min_y;

    const Vertex verts[3] = { t.v0, t.v1, t.v2 };
    for (u32 i = 1; i < 3; ++i) {
        const i32 vx = static_cast<i32>(verts[i].x);
        const i32 vy = static_cast<i32>(verts[i].y);
        if (vx < min_x) min_x = vx;
        if (vx > max_x) max_x = vx;
        if (vy < min_y) min_y = vy;
        if (vy > max_y) max_y = vy;
    }

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= static_cast<i32>(g_w)) max_x = static_cast<i32>(g_w) - 1;
    if (max_y >= static_cast<i32>(g_h)) max_y = static_cast<i32>(g_h) - 1;

    const float area = (t.v1.x - t.v0.x) * (t.v2.y - t.v0.y)
                     - (t.v2.x - t.v0.x) * (t.v1.y - t.v0.y);
    if (area == 0.0f) return;

    const bool ccw = area > 0.0f;

    const u32 r0 = (t.v0.color >> 16) & 0xFF;
    const u32 g0 = (t.v0.color >>  8) & 0xFF;
    const u32 b0 =  t.v0.color        & 0xFF;
    const u32 r1 = (t.v1.color >> 16) & 0xFF;
    const u32 g1 = (t.v1.color >>  8) & 0xFF;
    const u32 b1 =  t.v1.color        & 0xFF;
    const u32 r2 = (t.v2.color >> 16) & 0xFF;
    const u32 g2 = (t.v2.color >>  8) & 0xFF;
    const u32 b2 =  t.v2.color        & 0xFF;

    for (i32 y = min_y; y <= max_y; ++y) {
        for (i32 x = min_x; x <= max_x; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;

            const i32 w0 = edge_fn(t.v0, t.v1, px, py);
            const i32 w1 = edge_fn(t.v1, t.v2, px, py);
            const i32 w2 = edge_fn(t.v2, t.v0, px, py);

            const bool inside = ccw ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                    : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;

            const float l0 = static_cast<float>(w1) / area;
            const float l1 = static_cast<float>(w2) / area;
            const float l2 = static_cast<float>(w0) / area;

            const u32 r = static_cast<u32>(
                static_cast<float>(r0) * l0 +
                static_cast<float>(r1) * l1 +
                static_cast<float>(r2) * l2);
            const u32 g = static_cast<u32>(
                static_cast<float>(g0) * l0 +
                static_cast<float>(g1) * l1 +
                static_cast<float>(g2) * l2);
            const u32 b = static_cast<u32>(
                static_cast<float>(b0) * l0 +
                static_cast<float>(b1) * l1 +
                static_cast<float>(b2) * l2);

            put(x, y, (r << 16) | (g << 8) | b);
        }
    }
}

void draw_quad(const Vertex& v0, const Vertex& v1,
               const Vertex& v2, const Vertex& v3) noexcept {
    draw_triangle({v0, v1, v2});
    draw_triangle({v0, v2, v3});
}

const char* backend_name() noexcept { return "software-rasterizer"; }
u64 frames_rendered() noexcept { return g_frames; }

} // namespace notyvos::gpu