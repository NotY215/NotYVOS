#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/ps3/rsx/rsx.hpp>

namespace notyvos::ps3::rsx
{

namespace
{

u32 g_regs[kRegisterCount] = {};
Fifo g_fifo = {};
Surface g_target = {};

u32* g_pixels = nullptr;
u32 g_pix_w = 0;
u32 g_pix_h = 0;
u32 g_pix_pitch = 0;

u32* g_depth = nullptr;
u32 g_depth_clear = 0xFFFFFFFFu;
bool g_depth_on = false;

Primitive g_prim = Primitive::Triangles;
Interp g_interp = Interp::Flat;
Vertex g_verts[64];
u32 g_vert_count = 0;

u64 g_vb_addr = 0;
u64 g_ib_addr = 0;
u32 g_vb_stride = 16;

i32 g_scissor_x = 0;
i32 g_scissor_y = 0;
i32 g_scissor_w = 0;
i32 g_scissor_h = 0;

constexpr u32 kMaxMipLevels = 6;   // 64 -> 32 -> 16 -> 8 -> 4 -> 2
constexpr i32 kFixedOne = 0x10000; // 16.16 fixed-point 1.0

u64 g_tex_addr = 0;
u32 g_tex_w = 0;
u32 g_tex_h = 0;
bool g_tex_on = false;
Wrap g_tex_wrap = Wrap::Repeat;
i32 g_tex_lod_bias = 0; // 16.16
u32 g_mip_count = 0;
u32 g_mip_w[kMaxMipLevels] = {};
u32 g_mip_h[kMaxMipLevels] = {};
u32 g_tex_mip[kMaxMipLevels][64 * 64] = {}; // level 0 = original

// UV / W staging: kVertexUV and kVertexW set these, the next kVertexPush
// consumes them.
i32 g_pending_u = 0;
i32 g_pending_v = 0;
i32 g_pending_w = kFixedOne;

GuestRead32Fn g_guest_read32 = nullptr;
void* g_guest_user = nullptr;

u64 g_commands = 0;
u64 g_draws = 0;
u64 g_presents = 0;
u64 g_clears = 0;
u64 g_unknowns = 0;
u64 g_primitives = 0;
u64 g_pixels_written = 0;
bool g_ready = false;

constexpr u32 kMaxUnknownLogs = 8;
u32 g_unknown_logs = 0;

inline u32 next_index(u32 idx) noexcept
{
    return (idx + 1u) % kFifoWords;
}

inline u32 fifo_used() noexcept
{
    const u32 g = g_fifo.jump_pending ? g_fifo.jump_target : g_fifo.get;
    const u32 p = g_fifo.put;
    return (p >= g) ? (p - g) : (kFifoWords - (g - p));
}

bool fifo_pop(u32& out) noexcept
{
    if (g_fifo.jump_pending)
    {
        g_fifo.get = g_fifo.jump_target;
        g_fifo.jump_pending = false;
    }
    if (g_fifo.put == g_fifo.get)
        return false;
    out = g_fifo.buffer[g_fifo.get];
    g_fifo.get = next_index(g_fifo.get);
    ++g_fifo.total_words_out;
    return true;
}

bool fifo_peek(u32& out) noexcept
{
    if (g_fifo.jump_pending)
    {
        if (g_fifo.jump_target == g_fifo.put)
            return false;
        out = g_fifo.buffer[g_fifo.jump_target];
        return true;
    }
    if (g_fifo.put == g_fifo.get)
        return false;
    out = g_fifo.buffer[g_fifo.get];
    return true;
}

// --- Depth test ------------------------------------------------------------
inline bool depth_test(i32 x, i32 y, u32 z) noexcept
{
    if (!g_depth_on || !g_depth)
        return true;
    if (x < 0 || y < 0)
        return false;
    if (static_cast<u32>(x) >= g_pix_w)
        return false;
    if (static_cast<u32>(y) >= g_pix_h)
        return false;
    const usize idx = static_cast<usize>(y) * g_pix_w + static_cast<usize>(x);
    if (z < g_depth[idx])
    {
        g_depth[idx] = z;
        return true;
    }
    return false;
}

// --- Texture sampler -------------------------------------------------------

// LOD selection uses the fraction of texture space covered by the whole
// primitive. The rasterizer passes the box size in 1/16-pixel units; we
// derive a level via log2(span / texture dimension). `extra` is the
// current LOD bias (16.16).
inline u32 sample_tex(i32 u16, i32 v16, i32 lod_bias, u32 span16) noexcept
{
    if (!g_tex_on || g_tex_w == 0 || g_tex_h == 0)
        return 0x00FFFFFFu;

    // Choose a mip level.
    u32 level = 0;
    if (g_mip_count > 1u && span16 > 0)
    {
        const u32 span = static_cast<u32>(span16 >> 4);
        const u32 dim = (g_tex_w > g_tex_h) ? g_tex_w : g_tex_h;
        u32 d = dim;
        u32 l = 0;
        while (d > 1u && span < d && l + 1u < g_mip_count)
        {
            d >>= 1;
            ++l;
        }
        level = l;
        if (lod_bias != 0)
        {
            const i32 bump = lod_bias / kFixedOne;
            if (bump > 0 && level + static_cast<u32>(bump) < g_mip_count)
                level += static_cast<u32>(bump);
            else if (bump < 0)
            {
                const u32 mag = static_cast<u32>(-bump);
                level = (mag >= level) ? 0u : (level - mag);
            }
        }
    }
    if (level >= g_mip_count)
        level = g_mip_count ? g_mip_count - 1u : 0u;

    const u32 tw = g_mip_w[level];
    const u32 th = g_mip_h[level];
    if (tw == 0 || th == 0)
        return 0x00FFFFFFu;

    i32 tx = u16 >> 4;
    i32 ty = v16 >> 4;

    if (g_tex_wrap == Wrap::Repeat)
    {
        tx %= static_cast<i32>(tw);
        if (tx < 0)
            tx += static_cast<i32>(tw);
        ty %= static_cast<i32>(th);
        if (ty < 0)
            ty += static_cast<i32>(th);
    }
    else
    {
        if (tx < 0)
            tx = 0;
        if (tx >= static_cast<i32>(tw))
            tx = static_cast<i32>(tw) - 1;
        if (ty < 0)
            ty = 0;
        if (ty >= static_cast<i32>(th))
            ty = static_cast<i32>(th) - 1;
    }

    return g_tex_mip[level][static_cast<u32>(ty) * 64u + static_cast<u32>(tx)];
}

inline void plot(i32 x, i32 y, u32 z, u32 color, i32 u16, i32 v16, u32 span16) noexcept
{
    if (!g_pixels)
        return;
    if (x < 0 || y < 0)
        return;
    if (static_cast<u32>(x) >= g_pix_w)
        return;
    if (static_cast<u32>(y) >= g_pix_h)
        return;
    if (g_scissor_w > 0 && g_scissor_h > 0)
    {
        if (x < g_scissor_x || y < g_scissor_y)
            return;
        if (x >= g_scissor_x + g_scissor_w)
            return;
        if (y >= g_scissor_y + g_scissor_h)
            return;
    }
    if (!depth_test(x, y, z))
        return;

    u32 out = color & 0x00FFFFFFu;
    if (g_tex_on)
        out = sample_tex(u16, v16, g_tex_lod_bias, span16);

    g_pixels[static_cast<usize>(y) * g_pix_pitch + static_cast<usize>(x)] = out;
    ++g_pixels_written;
}

// --- Raster primitives -----------------------------------------------------

void raster_line(const Vertex& a, const Vertex& b) noexcept
{
    i32 x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    const u32 z = a.z;
    const u32 c = a.color;
    const i32 u = a.u;
    const i32 v = a.v;

    i32 dx = x1 - x0, dy = y1 - y0;
    const i32 sx = dx < 0 ? -1 : 1;
    const i32 sy = dy < 0 ? -1 : 1;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    i32 err = (dx > dy ? dx : -dy) / 2;

    // Span in 1/16-pixel units used as the LOD input. Cheap approximation:
    // Chebyshev length of the segment.
    const i32 adx = (dx > dy) ? dx : dy;
    const u32 span16 = static_cast<u32>(adx > 0 ? adx : 1);

    for (;;)
    {
        plot(x0, y0, z, c, u, v, span16);
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

// Barycentric triangle rasterizer.
//
//   w0 = 2 * signed_area(a, b, p)
//   w1 = 2 * signed_area(b, c, p)
//   w2 = 2 * signed_area(c, a, p)
//
//   lambda_a = w1 / area
//   lambda_b = w2 / area
//   lambda_c = w0 / area
//
// Interpolated attribute = (w1 * a.attr + w2 * b.attr + w0 * c.attr) / area.
//
// The 6D code used (w0, w1, w2) as the multipliers for (a, b, c), which
// rotates the color correspondence on non-uniform triangles. Fixed here.
void raster_triangle(const Vertex& a, const Vertex& b, const Vertex& c) noexcept
{
    if (!g_pixels)
        return;

    i32 min_x = a.x, max_x = a.x, min_y = a.y, max_y = a.y;
    const Vertex vs[2] = {b, c};
    for (u32 i = 0; i < 2; ++i)
    {
        if (vs[i].x < min_x)
            min_x = vs[i].x;
        if (vs[i].x > max_x)
            max_x = vs[i].x;
        if (vs[i].y < min_y)
            min_y = vs[i].y;
        if (vs[i].y > max_y)
            max_y = vs[i].y;
    }
    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x >= static_cast<i32>(g_pix_w))
        max_x = static_cast<i32>(g_pix_w) - 1;
    if (max_y >= static_cast<i32>(g_pix_h))
        max_y = static_cast<i32>(g_pix_h) - 1;

    const i64 area = static_cast<i64>(b.x - a.x) * static_cast<i64>(c.y - a.y) -
                     static_cast<i64>(c.x - a.x) * static_cast<i64>(b.y - a.y);
    if (area == 0)
        return;

    const bool ccw = area > 0;
    const bool smooth = (g_interp == Interp::Smooth);

    const u32 r0 = (a.color >> 16) & 0xFFu, g0 = (a.color >> 8) & 0xFFu, b0 = a.color & 0xFFu;
    const u32 r1 = (b.color >> 16) & 0xFFu, g1 = (b.color >> 8) & 0xFFu, b1 = b.color & 0xFFu;
    const u32 r2 = (c.color >> 16) & 0xFFu, g2 = (c.color >> 8) & 0xFFu, b2 = c.color & 0xFFu;

    for (i32 y = min_y; y <= max_y; ++y)
    {
        for (i32 x = min_x; x <= max_x; ++x)
        {
            const i64 w0 = static_cast<i64>(b.x - a.x) * static_cast<i64>(y - a.y) -
                           static_cast<i64>(b.y - a.y) * static_cast<i64>(x - a.x);
            const i64 w1 = static_cast<i64>(c.x - b.x) * static_cast<i64>(y - b.y) -
                           static_cast<i64>(c.y - b.y) * static_cast<i64>(x - b.x);
            const i64 w2 = static_cast<i64>(a.x - c.x) * static_cast<i64>(y - c.y) -
                           static_cast<i64>(a.y - c.y) * static_cast<i64>(x - c.x);
            const bool inside =
                ccw ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside)
                continue;

            u32 col;
            if (smooth)
            {
                // (w1, w2, w0) map to (a, b, c).
                const i64 r_acc = static_cast<i64>(r0) * w1 + static_cast<i64>(r1) * w2 +
                                  static_cast<i64>(r2) * w0;
                const i64 g_acc = static_cast<i64>(g0) * w1 + static_cast<i64>(g1) * w2 +
                                  static_cast<i64>(g2) * w0;
                const i64 b_acc = static_cast<i64>(b0) * w1 + static_cast<i64>(b1) * w2 +
                                  static_cast<i64>(b2) * w0;
                const u32 rr = static_cast<u32>(r_acc / area);
                const u32 gg = static_cast<u32>(g_acc / area);
                const u32 bb = static_cast<u32>(b_acc / area);
                col = (rr << 16) | (gg << 8) | bb;
            }
            else
            {
                col = a.color;
            }

            // Perspective-correct UV interpolation.
            //
            //   s/w  = sum(lam_i * u_i / w_i)
            //   t/w  = sum(lam_i * v_i / w_i)
            //   1/w  = sum(lam_i / w_i)
            //   u    = (s/w) / (1/w)
            //
            // w_i is 16.16 fixed point. To keep everything in integer
            // arithmetic we scale the products up by kFixedOne.
            const i64 wa = (a.w > 0) ? a.w : kFixedOne;
            const i64 wb = (b.w > 0) ? b.w : kFixedOne;
            const i64 wc = (c.w > 0) ? c.w : kFixedOne;

            const i64 sw_acc = static_cast<i64>(a.u) * kFixedOne * w1 / wa +
                               static_cast<i64>(b.u) * kFixedOne * w2 / wb +
                               static_cast<i64>(c.u) * kFixedOne * w0 / wc;
            const i64 tw_acc = static_cast<i64>(a.v) * kFixedOne * w1 / wa +
                               static_cast<i64>(b.v) * kFixedOne * w2 / wb +
                               static_cast<i64>(c.v) * kFixedOne * w0 / wc;
            const i64 iw_acc = static_cast<i64>(kFixedOne) * kFixedOne * w1 / wa +
                               static_cast<i64>(kFixedOne) * kFixedOne * w2 / wb +
                               static_cast<i64>(kFixedOne) * kFixedOne * w0 / wc;

            i32 uu = 0, vv = 0;
            if (iw_acc != 0)
            {
                uu = static_cast<i32>((sw_acc / area) / (iw_acc / area));
                vv = static_cast<i32>((tw_acc / area) / (iw_acc / area));
            }

            // Span estimate for LOD: triangle bbox diagonal in 1/16-pixel
            // units. The texture bind uses UV spread x dim in Phase 6G; for
            // 6F we use the pixel span.
            const u32 span16 = static_cast<u32>((max_x - min_x) + (max_y - min_y));

            plot(x, y, a.z, col, uu, vv, span16);
        }
    }
    ++g_primitives;
}

void assemble_and_raster() noexcept
{
    if (!g_pixels)
    {
        g_vert_count = 0;
        return;
    }
    switch (g_prim)
    {
    case Primitive::Points:
        for (u32 i = 0; i < g_vert_count; ++i)
        {
            const Vertex& v = g_verts[i];
            plot(v.x, v.y, v.z, v.color, v.u, v.v, 16u);
        }
        break;
    case Primitive::Lines:
        for (u32 i = 0; i + 1 < g_vert_count; i += 2)
            raster_line(g_verts[i], g_verts[i + 1]);
        break;
    case Primitive::Triangles:
        for (u32 i = 0; i + 2 < g_vert_count; i += 3)
            raster_triangle(g_verts[i], g_verts[i + 1], g_verts[i + 2]);
        break;
    case Primitive::TriStrip:
        for (u32 i = 0; i + 2 < g_vert_count; ++i)
        {
            if ((i & 1u) == 0)
                raster_triangle(g_verts[i], g_verts[i + 1], g_verts[i + 2]);
            else
                raster_triangle(g_verts[i + 1], g_verts[i], g_verts[i + 2]);
        }
        break;
    case Primitive::TriFan:
        for (u32 i = 1; i + 1 < g_vert_count; ++i)
            raster_triangle(g_verts[0], g_verts[i], g_verts[i + 1]);
        break;
    }
    g_vert_count = 0;
}

// --- Guest-memory vertex fetch --------------------------------------------

bool guest_read_vertex(u64 addr, Vertex& out) noexcept
{
    if (!g_guest_read32)
        return false;
    u32 w[7] = {0, 0, 0, 0, 0, 0, 0};
    for (u32 i = 0; i < 4; ++i)
        if (!g_guest_read32(g_guest_user, addr + i * 4u, &w[i]))
            return false;
    out.x = static_cast<i32>(w[0]);
    out.y = static_cast<i32>(w[1]);
    out.z = w[2];
    out.color = w[3] & 0x00FFFFFFu;

    // Optional UV extension at bytes 16..23 (stride >= 24).
    if (g_vb_stride >= 24)
    {
        for (u32 i = 4; i < 6; ++i)
            if (!g_guest_read32(g_guest_user, addr + i * 4u, &w[i]))
                return false;
        out.u = static_cast<i32>(w[4]);
        out.v = static_cast<i32>(w[5]);
    }
    else
    {
        out.u = 0;
        out.v = 0;
    }

    // Optional W at byte 24 (stride >= 28). Default 1.0 when absent.
    if (g_vb_stride >= 28)
    {
        if (!g_guest_read32(g_guest_user, addr + 6u * 4u, &w[6]))
            return false;
        const i32 ww = static_cast<i32>(w[6]);
        out.w = (ww > 0) ? ww : kFixedOne;
    }
    else
        out.w = kFixedOne;

    return true;
}

void emit_vertex(const Vertex& v) noexcept
{
    if (g_vert_count >= 64)
        return;
    g_verts[g_vert_count++] = v;
}

// --- Method handlers -------------------------------------------------------

void handle_nop(u32, const u32*) noexcept {}

void handle_object(u32 count, const u32* data) noexcept
{
    if (count >= 1 && g_unknown_logs < 2)
        log::write(log::Level::Info, "rsx", "object class=0x%llx",
                   static_cast<unsigned long long>(data[0]));
}

void handle_surface_format(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_target.format = static_cast<SurfaceFormat>(data[0] & 0xFFu);
    g_target.valid = false;
}

void handle_surface_col(u32 count, const u32* data) noexcept
{
    if (count < 2)
        return;
    const u64 lo = static_cast<u64>(data[0]);
    const u64 hi = static_cast<u64>(data[1]);
    g_target.address = (hi << 32) | lo;
    g_target.valid = false;
}

void handle_surface_pitch(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_target.pitch = data[0] & 0x0000FFFFu;
    g_target.valid = false;
}

void handle_surface_w(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_target.width = data[0] & 0x0000FFFFu;
}

void handle_surface_h(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_target.height = data[0] & 0x0000FFFFu;
    if (g_target.width != 0 && g_target.height != 0 && g_target.pitch != 0 && g_target.address != 0)
        g_target.valid = true;
}

void handle_clear_color(u32 count, const u32* data) noexcept
{
    if (count >= 1)
        g_regs[method::kClearColor / 4] = data[0];
}

void handle_background(u32 count, const u32* data) noexcept
{
    if (count >= 1)
        g_regs[method::kBackground / 4] = data[0];
}

void handle_clear(u32, const u32*) noexcept
{
    ++g_clears;
    if (!g_pixels)
        return;
    const u32 color = g_regs[method::kClearColor / 4] & 0x00FFFFFFu;
    for (u32 y = 0; y < g_pix_h; ++y)
    {
        u32* row = g_pixels + static_cast<usize>(y) * g_pix_pitch;
        for (u32 x = 0; x < g_pix_w; ++x)
            row[x] = color;
    }
    if (g_depth)
    {
        const usize n = static_cast<usize>(g_pix_w) * g_pix_h;
        for (usize i = 0; i < n; ++i)
            g_depth[i] = g_depth_clear;
    }
    g_pixels_written += static_cast<u64>(g_pix_w) * static_cast<u64>(g_pix_h);
}

void handle_prim_type(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_prim = static_cast<Primitive>(data[0] & 0x7u);
}

void handle_vertex_push(u32 count, const u32* data) noexcept
{
    if (count < 4)
        return;
    Vertex v{};
    v.x = static_cast<i32>(data[0]);
    v.y = static_cast<i32>(data[1]);
    v.z = data[2];
    v.color = data[3] & 0x00FFFFFFu;
    v.u = g_pending_u;
    v.v = g_pending_v;
    v.w = g_pending_w;
    emit_vertex(v);
}

void handle_vertex_uv(u32 count, const u32* data) noexcept
{
    if (count < 2)
        return;
    g_pending_u = static_cast<i32>(data[0]);
    g_pending_v = static_cast<i32>(data[1]);
}

void handle_vertex_w(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    const i32 w = static_cast<i32>(data[0]);
    g_pending_w = (w <= 0) ? kFixedOne : w;
}

void handle_texture_lod_bias(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_tex_lod_bias = static_cast<i32>(data[0]);
}

void handle_vertex_flush(u32, const u32*) noexcept
{
    assemble_and_raster();
}

void handle_draw(u32, const u32*) noexcept
{
    ++g_draws;
    assemble_and_raster();
}

void handle_present(u32, const u32*) noexcept
{
    ++g_presents;
}

void handle_jump(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_fifo.jump_target = data[0] % kFifoWords;
    g_fifo.jump_pending = true;
}

void handle_call(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    if (g_fifo.call_sp < kCallDepth)
        g_fifo.call_stack[g_fifo.call_sp++] = g_fifo.get;
    g_fifo.jump_target = data[0] % kFifoWords;
    g_fifo.jump_pending = true;
}

void handle_return(u32, const u32*) noexcept
{
    if (g_fifo.call_sp == 0)
        return;
    g_fifo.jump_target = g_fifo.call_stack[--g_fifo.call_sp];
    g_fifo.jump_pending = true;
}

void handle_vertex_buffer(u32 count, const u32* data) noexcept
{
    if (count < 2)
        return;
    g_vb_addr = (static_cast<u64>(data[1]) << 32) | static_cast<u64>(data[0]);
}

void handle_index_buffer(u32 count, const u32* data) noexcept
{
    if (count < 2)
        return;
    g_ib_addr = (static_cast<u64>(data[1]) << 32) | static_cast<u64>(data[0]);
}

void handle_vertex_stride(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    const u32 s = data[0];
    if (s >= 16 && s <= 256)
        g_vb_stride = s;
}

void handle_draw_arrays(u32 count, const u32* data) noexcept
{
    ++g_draws;
    if (count < 1 || !g_guest_read32)
        return;
    const u32 n = data[0];
    if (n == 0)
        return;
    for (u32 i = 0; i < n && g_vert_count < 64; ++i)
    {
        Vertex v{};
        if (!guest_read_vertex(g_vb_addr + static_cast<u64>(i) * g_vb_stride, v))
            break;
        emit_vertex(v);
    }
    assemble_and_raster();
}

void handle_draw_elements(u32 count, const u32* data) noexcept
{
    ++g_draws;
    if (count < 1 || !g_guest_read32)
        return;
    const u32 n = data[0];
    if (n == 0)
        return;
    for (u32 i = 0; i < n && g_vert_count < 64; ++i)
    {
        u32 idx = 0;
        if (!g_guest_read32(g_guest_user, g_ib_addr + static_cast<u64>(i) * 4u, &idx))
            break;
        Vertex v{};
        if (!guest_read_vertex(g_vb_addr + static_cast<u64>(idx) * g_vb_stride, v))
            break;
        emit_vertex(v);
    }
    assemble_and_raster();
}

void handle_depth_enable(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_depth_on = (data[0] != 0);
}

void handle_depth_clear(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_depth_clear = data[0];
}

void handle_interp_mode(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_interp = static_cast<Interp>(data[0] & 1u);
}

void handle_scissor_x(u32 c, const u32* d) noexcept
{
    if (c >= 1)
        g_scissor_x = static_cast<i32>(d[0]);
}
void handle_scissor_y(u32 c, const u32* d) noexcept
{
    if (c >= 1)
        g_scissor_y = static_cast<i32>(d[0]);
}
void handle_scissor_w(u32 c, const u32* d) noexcept
{
    if (c >= 1)
        g_scissor_w = static_cast<i32>(d[0]);
}
void handle_scissor_h(u32 c, const u32* d) noexcept
{
    if (c >= 1)
        g_scissor_h = static_cast<i32>(d[0]);
}

void handle_texture_bind(u32 count, const u32* data) noexcept
{
    if (count < 4)
        return;
    g_tex_addr = (static_cast<u64>(data[1]) << 32) | static_cast<u64>(data[0]);
    g_tex_w = data[2] & 0x3Fu;
    g_tex_h = data[3] & 0x3Fu;
    if (g_tex_w == 0 || g_tex_h == 0)
    {
        g_tex_on = false;
        return;
    }

    // Level 0: fetch original from guest memory.
    for (u32 y = 0; y < g_tex_h; ++y)
        for (u32 x = 0; x < g_tex_w; ++x)
        {
            u32 px = 0;
            if (g_guest_read32)
                (void)g_guest_read32(g_guest_user,
                                     g_tex_addr + (static_cast<u64>(y) * g_tex_w + x) * 4u, &px);
            g_tex_mip[0][y * 64u + x] = px & 0x00FFFFFFu;
        }

    // Build the mip chain with a 2x2 box filter until we hit 1x1 or the
    // level cap. Every level is stored in the same 64x64 slot layout.
    g_mip_w[0] = g_tex_w;
    g_mip_h[0] = g_tex_h;
    u32 levels = 1;
    while (levels < kMaxMipLevels && g_mip_w[levels - 1] > 1u && g_mip_h[levels - 1] > 1u)
    {
        const u32 pw = g_mip_w[levels - 1];
        const u32 ph = g_mip_h[levels - 1];
        const u32 nw = pw / 2u;
        const u32 nh = ph / 2u;
        for (u32 y = 0; y < nh; ++y)
            for (u32 x = 0; x < nw; ++x)
            {
                const u32 c00 = g_tex_mip[levels - 1][(y * 2u) * 64u + (x * 2u)];
                const u32 c10 = g_tex_mip[levels - 1][(y * 2u) * 64u + (x * 2u + 1u)];
                const u32 c01 = g_tex_mip[levels - 1][(y * 2u + 1u) * 64u + (x * 2u)];
                const u32 c11 = g_tex_mip[levels - 1][(y * 2u + 1u) * 64u + (x * 2u + 1u)];
                const u32 r = (((c00 >> 16) & 0xFFu) + ((c10 >> 16) & 0xFFu) +
                               ((c01 >> 16) & 0xFFu) + ((c11 >> 16) & 0xFFu)) /
                              4u;
                const u32 g = (((c00 >> 8) & 0xFFu) + ((c10 >> 8) & 0xFFu) + ((c01 >> 8) & 0xFFu) +
                               ((c11 >> 8) & 0xFFu)) /
                              4u;
                const u32 b = ((c00 & 0xFFu) + (c10 & 0xFFu) + (c01 & 0xFFu) + (c11 & 0xFFu)) / 4u;
                g_tex_mip[levels][y * 64u + x] = (r << 16) | (g << 8) | b;
            }
        g_mip_w[levels] = nw;
        g_mip_h[levels] = nh;
        ++levels;
    }
    g_mip_count = levels;
}

void handle_texture_enable(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_tex_on = (data[0] != 0);
}

void handle_texture_wrap(u32 count, const u32* data) noexcept
{
    if (count < 1)
        return;
    g_tex_wrap = static_cast<Wrap>(data[0] & 1u);
}

void handle_unknown(u32 byte_offset) noexcept
{
    ++g_unknowns;
    if (g_unknown_logs < kMaxUnknownLogs)
    {
        ++g_unknown_logs;
        log::write(log::Level::Warn, "rsx", "unknown method 0x%llx (rate-limited)",
                   static_cast<unsigned long long>(byte_offset));
    }
}

using Handler = void (*)(u32, const u32*);

Handler handler_for(u32 byte_offset) noexcept
{
    switch (byte_offset)
    {
    case method::kNop:
        return &handle_nop;
    case method::kObject:
        return &handle_object;
    case method::kSurfaceFmt:
        return &handle_surface_format;
    case method::kSurfaceCol:
        return &handle_surface_col;
    case method::kSurfacePit:
        return &handle_surface_pitch;
    case method::kSurfaceW:
        return &handle_surface_w;
    case method::kSurfaceH:
        return &handle_surface_h;
    case method::kClearColor:
        return &handle_clear_color;
    case method::kClear:
        return &handle_clear;
    case method::kDraw:
        return &handle_draw;
    case method::kPresent:
        return &handle_present;
    case method::kFifoJump:
        return &handle_jump;
    case method::kFifoCall:
        return &handle_call;
    case method::kFifoReturn:
        return &handle_return;
    case method::kPrimType:
        return &handle_prim_type;
    case method::kVertexPush:
        return &handle_vertex_push;
    case method::kVertexUV:
        return &handle_vertex_uv;
    case method::kVertexW:
        return &handle_vertex_w;
    case method::kVertexFlush:
        return &handle_vertex_flush;
    case method::kBackground:
        return &handle_background;
    case method::kVertexBuffer:
        return &handle_vertex_buffer;
    case method::kIndexBuffer:
        return &handle_index_buffer;
    case method::kVertexStride:
        return &handle_vertex_stride;
    case method::kDrawArrays:
        return &handle_draw_arrays;
    case method::kDrawElements:
        return &handle_draw_elements;
    case method::kDepthEnable:
        return &handle_depth_enable;
    case method::kDepthClear:
        return &handle_depth_clear;
    case method::kInterpMode:
        return &handle_interp_mode;
    case method::kScissorX:
        return &handle_scissor_x;
    case method::kScissorY:
        return &handle_scissor_y;
    case method::kScissorW:
        return &handle_scissor_w;
    case method::kScissorH:
        return &handle_scissor_h;
    case method::kTextureBind:
        return &handle_texture_bind;
    case method::kTextureEnable:
        return &handle_texture_enable;
    case method::kTextureWrap:
        return &handle_texture_wrap;
    case method::kTextureLodBias:
        return &handle_texture_lod_bias;
    default:
        return nullptr;
    }
}

} // namespace

void Rsx::init() noexcept
{
    libk::memset(g_regs, 0, sizeof(g_regs));
    libk::memset(&g_fifo, 0, sizeof(g_fifo));
    libk::memset(&g_target, 0, sizeof(g_target));
    libk::memset(g_verts, 0, sizeof(g_verts));
    libk::memset(g_tex_mip, 0, sizeof(g_tex_mip));

    g_target.format = SurfaceFormat::Unknown;
    g_commands = 0;
    g_draws = 0;
    g_presents = 0;
    g_clears = 0;
    g_unknowns = 0;
    g_primitives = 0;
    g_pixels_written = 0;
    g_vert_count = 0;
    g_unknown_logs = 0;
    g_vb_addr = 0;
    g_ib_addr = 0;
    g_vb_stride = 16;
    g_prim = Primitive::Triangles;
    g_interp = Interp::Flat;
    g_depth_on = false;
    g_depth_clear = 0xFFFFFFFFu;
    g_scissor_x = g_scissor_y = 0;
    g_scissor_w = g_scissor_h = 0;
    g_tex_addr = 0;
    g_tex_w = 0;
    g_tex_h = 0;
    g_tex_on = false;
    g_tex_wrap = Wrap::Repeat;
    g_tex_lod_bias = 0;
    g_mip_count = 0;
    for (u32 i = 0; i < kMaxMipLevels; ++i)
    {
        g_mip_w[i] = 0;
        g_mip_h[i] = 0;
    }
    g_pending_u = 0;
    g_pending_v = 0;
    g_pending_w = kFixedOne;
    g_guest_read32 = nullptr;
    g_guest_user = nullptr;
    g_ready = true;

    log::write(log::Level::Info, "rsx", "Phase 6A/6B/6C/6D/6E init: %llu regs, FIFO %llu words",
               static_cast<unsigned long long>(kRegisterCount),
               static_cast<unsigned long long>(kFifoWords));
}

bool Rsx::ready() noexcept
{
    return g_ready;
}

void Rsx::bind_surface_memory(u32* pixels, u32 width, u32 height, u32 pitch) noexcept
{
    g_pixels = pixels;
    g_pix_w = width;
    g_pix_h = height;
    g_pix_pitch = (pitch != 0) ? pitch : width;

    if (g_depth)
    {
        mm::Heap::deallocate(g_depth);
        g_depth = nullptr;
    }
    const usize n = static_cast<usize>(width) * height;
    if (n > 0)
    {
        g_depth = static_cast<u32*>(mm::Heap::allocate(n * sizeof(u32)));
        if (g_depth)
            for (usize i = 0; i < n; ++i)
                g_depth[i] = g_depth_clear;
    }
}

void Rsx::set_guest_read32(GuestRead32Fn fn, void* user) noexcept
{
    g_guest_read32 = fn;
    g_guest_user = user;
}

bool Rsx::texture_bound() noexcept
{
    return g_tex_on;
}

u32 Rsx::mip_levels() noexcept
{
    return g_mip_count;
}

u32 Rsx::mip_width(u32 level) noexcept
{
    return (level < g_mip_count) ? g_mip_w[level] : 0u;
}

u32 Rsx::mip_height(u32 level) noexcept
{
    return (level < g_mip_count) ? g_mip_h[level] : 0u;
}

bool Rsx::push(u32 word) noexcept
{
    if (!g_ready)
        return false;
    const u32 next = next_index(g_fifo.put);
    if (next == g_fifo.get)
        return false;
    g_fifo.buffer[g_fifo.put] = word;
    g_fifo.put = next;
    ++g_fifo.total_words_in;
    return true;
}

u32 Rsx::process(u32 max_commands) noexcept
{
    if (!g_ready)
        return 0;
    u32 processed = 0;
    while (processed < max_commands)
    {
        u32 header = 0;
        if (!fifo_peek(header))
            break;

        const u32 method_idx = header & 0x3FFFu;
        const u32 count = ((header >> 16) & 0x3u) + 1u;

        if (fifo_used() < (count + 1u))
            break;

        (void)fifo_pop(header);

        u32 data[4] = {0, 0, 0, 0};
        for (u32 i = 0; i < count; ++i)
        {
            u32 word = 0;
            if (!fifo_pop(word))
                break;
            data[i] = word;
        }

        const u32 byte_off = method_idx << 2;

        for (u32 i = 0; i < count; ++i)
        {
            const u32 idx = method_idx + i;
            if (idx < kRegisterCount)
                g_regs[idx] = data[i];
        }

        Handler h = handler_for(byte_off);
        if (h)
            h(count, data);
        else
            handle_unknown(byte_off);

        ++processed;
        ++g_commands;
    }
    return processed;
}

u32 Rsx::read_reg(u32 byte_offset) noexcept
{
    const u32 idx = byte_offset >> 2;
    return (idx < kRegisterCount) ? g_regs[idx] : 0u;
}

u64 Rsx::commands() noexcept
{
    return g_commands;
}
u64 Rsx::draws() noexcept
{
    return g_draws;
}
u64 Rsx::presents() noexcept
{
    return g_presents;
}
u64 Rsx::clears() noexcept
{
    return g_clears;
}
u64 Rsx::unknowns() noexcept
{
    return g_unknowns;
}
u64 Rsx::fifo_depth() noexcept
{
    return static_cast<u64>(fifo_used());
}
u64 Rsx::primitives_drawn() noexcept
{
    return g_primitives;
}
u64 Rsx::pixels_written() noexcept
{
    return g_pixels_written;
}

u32 Rsx::snapshot(u32* out, u32 max_pixels) noexcept
{
    if (!out || !g_pixels)
        return 0;
    const u32 total = g_pix_w * g_pix_h;
    const u32 n = (max_pixels < total) ? max_pixels : total;
    for (u32 y = 0; y < g_pix_h && (y * g_pix_w) < n; ++y)
    {
        const u32* src = g_pixels + static_cast<usize>(y) * g_pix_pitch;
        for (u32 x = 0; x < g_pix_w; ++x)
        {
            const u32 idx = y * g_pix_w + x;
            if (idx >= n)
                break;
            out[idx] = src[x];
        }
    }
    return n;
}

const Surface& Rsx::target() noexcept
{
    return g_target;
}

} // namespace notyvos::ps3::rsx
