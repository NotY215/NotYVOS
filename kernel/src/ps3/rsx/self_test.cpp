#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/rsx/rsx.hpp>
#include <kernel/ps3/rsx/self_test.hpp>

namespace notyvos::ps3::rsx
{

namespace
{

constexpr u32 header(u32 byte_offset, u32 count) noexcept
{
    return ((byte_offset >> 2) & 0x3FFFu) | (((count - 1u) & 0x3u) << 16);
}

void put_cmd(u32 byte_offset, u32 count, const u32* data) noexcept
{
    (void)Rsx::push(header(byte_offset, count));
    for (u32 i = 0; i < count; ++i)
        (void)Rsx::push(data[i]);
}

// ---------------------------------------------------------------------------
// 6A: structural
// ---------------------------------------------------------------------------

void test_surface_pipeline() noexcept
{
    Rsx::init();
    const u32 fmt = static_cast<u32>(SurfaceFormat::A8R8G8B8);
    const u32 col[2] = {0x00100000u, 0u};
    const u32 pitch = 800u * 4u;
    const u32 width = 800u, height = 600u, clear = 0x00FF0000u;

    put_cmd(method::kSurfaceFmt, 1, &fmt);
    put_cmd(method::kSurfaceCol, 2, col);
    put_cmd(method::kSurfacePit, 1, &pitch);
    put_cmd(method::kSurfaceW, 1, &width);
    put_cmd(method::kSurfaceH, 1, &height);
    put_cmd(method::kClearColor, 1, &clear);
    put_cmd(method::kClear, 1, &width);
    put_cmd(method::kDraw, 1, &width);
    put_cmd(method::kPresent, 1, &width);

    const u32 done = Rsx::process(32);
    const Surface& s = Rsx::target();

    const bool surf_ok = s.valid && s.format == SurfaceFormat::A8R8G8B8 && s.address == 0x100000u &&
                         s.pitch == 3200u && s.width == 800u && s.height == 600u;
    const bool stats_ok = (Rsx::commands() >= 9) && (Rsx::clears() == 1) && (Rsx::draws() == 1) &&
                          (Rsx::presents() == 1) && (Rsx::unknowns() == 0);
    const bool ok = (done >= 9) && surf_ok && stats_ok;

    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX surface test: %s cmds=%llu clear=%llu draw=%llu present=%llu",
               ok ? "PASS" : "FAIL", static_cast<unsigned long long>(Rsx::commands()),
               static_cast<unsigned long long>(Rsx::clears()),
               static_cast<unsigned long long>(Rsx::draws()),
               static_cast<unsigned long long>(Rsx::presents()));
}

void test_fifo_jump() noexcept
{
    Rsx::init();
    const u32 zero = 0;
    put_cmd(method::kNop, 1, &zero);
    const u32 tgt = 4;
    put_cmd(method::kFifoJump, 1, &tgt);
    const u32 poison = 0xDEADBEEFu;
    put_cmd(method::kNop, 1, &poison);
    put_cmd(method::kDraw, 1, &zero);

    const u64 before = Rsx::draws();
    (void)Rsx::process(8);
    const bool ok = (Rsx::draws() == before + 1) && (Rsx::commands() >= 3);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX FIFO jump test: %s draws=%llu cmds=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(Rsx::draws()),
               static_cast<unsigned long long>(Rsx::commands()));
}

void test_fifo_call_return() noexcept
{
    // JUMP skips the sub so it isn't the entry point.
    //   [0..1] JUMP to word 6
    //   [2..3] DRAW          (sub body)
    //   [4..5] RETURN
    //   [6..7] CALL sub=2
    //   [8..9] PRESENT
    Rsx::init();
    const u32 zero = 0;
    const u32 skip = 6;
    const u32 sub = 2;

    put_cmd(method::kFifoJump, 1, &skip);
    put_cmd(method::kDraw, 1, &zero);
    put_cmd(method::kFifoReturn, 1, &zero);
    put_cmd(method::kFifoCall, 1, &sub);
    put_cmd(method::kPresent, 1, &zero);

    const u32 r = Rsx::process(16);
    const bool ok = (Rsx::draws() == 1) && (Rsx::presents() == 1) && (r >= 5);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX FIFO call/return test: %s draws=%llu presents=%llu processed=%llu",
               ok ? "PASS" : "FAIL", static_cast<unsigned long long>(Rsx::draws()),
               static_cast<unsigned long long>(Rsx::presents()),
               static_cast<unsigned long long>(r));
}

void test_unknown_tolerance() noexcept
{
    Rsx::init();
    const u32 zero = 0;
    constexpr u32 kBogus = 0x0FFC;
    put_cmd(kBogus, 1, &zero);
    put_cmd(method::kPresent, 1, &zero);
    (void)Rsx::process(4);
    const bool ok = (Rsx::unknowns() == 1) && (Rsx::presents() == 1);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX unknown-method test: %s unknowns=%llu presents=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(Rsx::unknowns()),
               static_cast<unsigned long long>(Rsx::presents()));
}

// ---------------------------------------------------------------------------
// Shared raster target + guest memory
// ---------------------------------------------------------------------------

constexpr u32 kFbW = 128;
constexpr u32 kFbH = 128;
alignas(64) u32 g_fb[kFbW * kFbH];

alignas(64) u8 g_guest[65536];

bool guest_read32(void* user, u64 addr, u32* out) noexcept
{
    (void)user;
    if (addr + 4 > sizeof(g_guest))
        return false;
    const u8* p = g_guest + addr;
    *out = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
    return true;
}

void write_guest_u32(u64 addr, u32 v) noexcept
{
    u8* p = g_guest + addr;
    p[0] = static_cast<u8>(v & 0xFFu);
    p[1] = static_cast<u8>((v >> 8) & 0xFFu);
    p[2] = static_cast<u8>((v >> 16) & 0xFFu);
    p[3] = static_cast<u8>((v >> 24) & 0xFFu);
}

// ---------------------------------------------------------------------------
// 6B / 8A / 8B: raster
// ---------------------------------------------------------------------------

void test_render_clear() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    const u32 color = 0x0000FF00u, dummy = 0;
    put_cmd(method::kClearColor, 1, &color);
    put_cmd(method::kClear, 1, &dummy);
    (void)Rsx::process(4);

    bool ok = true;
    for (u32 i = 0; i < kFbW * kFbH; ++i)
        if (g_fb[i] != color)
        {
            ok = false;
            break;
        }
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx", "RSX render clear test: %s",
               ok ? "PASS" : "FAIL");
}

void test_render_triangle() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 v0[4] = {10u, 10u, 0u, 0x00FF0000u};
    const u32 v1[4] = {110u, 10u, 0u, 0x00FF0000u};
    const u32 v2[4] = {10u, 110u, 0u, 0x00FF0000u};
    put_cmd(method::kVertexPush, 4, v0);
    put_cmd(method::kVertexPush, 4, v1);
    put_cmd(method::kVertexPush, 4, v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(16);
    const u32 in = g_fb[30 * kFbW + 30];
    const u32 out = g_fb[100 * kFbW + 100];
    const bool ok = (in == 0x00FF0000u) && (out == 0u);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX render triangle test: %s inside=0x%llx outside=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(in), static_cast<unsigned long long>(out));
}

void test_render_tristrip() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 prim = static_cast<u32>(Primitive::TriStrip);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 v0[4] = {20u, 20u, 0u, 0x000000FFu};
    const u32 v1[4] = {100u, 20u, 0u, 0x000000FFu};
    const u32 v2[4] = {20u, 100u, 0u, 0x000000FFu};
    const u32 v3[4] = {100u, 100u, 0u, 0x000000FFu};
    put_cmd(method::kVertexPush, 4, v0);
    put_cmd(method::kVertexPush, 4, v1);
    put_cmd(method::kVertexPush, 4, v2);
    put_cmd(method::kVertexPush, 4, v3);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(16);
    const u32 a = g_fb[30 * kFbW + 30];
    const u32 b = g_fb[90 * kFbW + 90];
    const bool ok = (a == 0x000000FFu) && (b == 0x000000FFu);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX render tristrip test: %s a=0x%llx b=0x%llx prims=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(a), static_cast<unsigned long long>(b),
               static_cast<unsigned long long>(Rsx::primitives_drawn()));
}

void test_render_line() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 prim = static_cast<u32>(Primitive::Lines);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 a[4] = {10u, 10u, 0u, 0x00FFFF00u};
    const u32 b[4] = {100u, 100u, 0u, 0x00FFFF00u};
    put_cmd(method::kVertexPush, 4, a);
    put_cmd(method::kVertexPush, 4, b);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(16);
    bool ok = true;
    for (i32 i = 20; i <= 90; ++i)
        if (g_fb[static_cast<u32>(i) * kFbW + static_cast<u32>(i)] != 0x00FFFF00u)
        {
            ok = false;
            break;
        }
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx", "RSX render line test: %s",
               ok ? "PASS" : "FAIL");
}

void test_render_points() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 prim = static_cast<u32>(Primitive::Points);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 p0[4] = {5u, 5u, 0u, 0x0000FFFFu};
    const u32 p1[4] = {50u, 50u, 0u, 0x00FF00FFu};
    const u32 p2[4] = {100u, 100u, 0u, 0x00FFFF00u};
    put_cmd(method::kVertexPush, 4, p0);
    put_cmd(method::kVertexPush, 4, p1);
    put_cmd(method::kVertexPush, 4, p2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(16);
    const bool ok = (g_fb[5u * kFbW + 5u] == 0x0000FFFFu) &&
                    (g_fb[50u * kFbW + 50u] == 0x00FF00FFu) &&
                    (g_fb[100u * kFbW + 100u] == 0x00FFFF00u);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx", "RSX render points test: %s",
               ok ? "PASS" : "FAIL");
}

void test_render_fan() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 prim = static_cast<u32>(Primitive::TriFan);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 c[4] = {60u, 60u, 0u, 0x0000FF00u};
    const u32 p1[4] = {20u, 60u, 0u, 0x0000FF00u};
    const u32 p2[4] = {60u, 20u, 0u, 0x0000FF00u};
    const u32 p3[4] = {100u, 60u, 0u, 0x0000FF00u};
    const u32 p4[4] = {60u, 100u, 0u, 0x0000FF00u};
    put_cmd(method::kVertexPush, 4, c);
    put_cmd(method::kVertexPush, 4, p1);
    put_cmd(method::kVertexPush, 4, p2);
    put_cmd(method::kVertexPush, 4, p3);
    put_cmd(method::kVertexPush, 4, p4);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(16);
    const bool ok = (g_fb[40 * kFbW + 50] == 0x0000FF00u) &&
                    (g_fb[50 * kFbW + 40] == 0x0000FF00u) &&
                    (g_fb[50 * kFbW + 70] == 0x0000FF00u) && (Rsx::primitives_drawn() >= 3);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX render fan test: %s prims=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(Rsx::primitives_drawn()));
}

// ---------------------------------------------------------------------------
// 6C: guest-memory vertex fetch
// ---------------------------------------------------------------------------

void test_draw_arrays() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    Rsx::set_guest_read32(&guest_read32, nullptr);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    write_guest_u32(0x1000 + 0u, 10u);
    write_guest_u32(0x1000 + 4u, 10u);
    write_guest_u32(0x1000 + 8u, 0u);
    write_guest_u32(0x1000 + 12u, 0x00FF00FFu);

    write_guest_u32(0x1010 + 0u, 100u);
    write_guest_u32(0x1010 + 4u, 10u);
    write_guest_u32(0x1010 + 8u, 0u);
    write_guest_u32(0x1010 + 12u, 0x00FF00FFu);

    write_guest_u32(0x1020 + 0u, 10u);
    write_guest_u32(0x1020 + 4u, 100u);
    write_guest_u32(0x1020 + 8u, 0u);
    write_guest_u32(0x1020 + 12u, 0x00FF00FFu);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 vb[2] = {0x1000u, 0u};
    put_cmd(method::kVertexBuffer, 2, vb);
    const u32 stride = 16;
    put_cmd(method::kVertexStride, 1, &stride);
    const u32 nv = 3;
    put_cmd(method::kDrawArrays, 1, &nv);

    (void)Rsx::process(16);
    const bool ok = (g_fb[30 * kFbW + 30] == 0x00FF00FFu) && (Rsx::primitives_drawn() == 1);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX draw-arrays test: %s prims=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(Rsx::primitives_drawn()));
}

void test_draw_elements() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    Rsx::set_guest_read32(&guest_read32, nullptr);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    write_guest_u32(0x2000 + 0u, 10u);
    write_guest_u32(0x2000 + 4u, 10u);
    write_guest_u32(0x2000 + 8u, 0u);
    write_guest_u32(0x2000 + 12u, 0x00808080u);

    write_guest_u32(0x2010 + 0u, 100u);
    write_guest_u32(0x2010 + 4u, 10u);
    write_guest_u32(0x2010 + 8u, 0u);
    write_guest_u32(0x2010 + 12u, 0x00808080u);

    write_guest_u32(0x2020 + 0u, 10u);
    write_guest_u32(0x2020 + 4u, 100u);
    write_guest_u32(0x2020 + 8u, 0u);
    write_guest_u32(0x2020 + 12u, 0x00808080u);

    write_guest_u32(0x3000 + 0u, 0u);
    write_guest_u32(0x3000 + 4u, 1u);
    write_guest_u32(0x3000 + 8u, 2u);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 vb[2] = {0x2000u, 0u};
    put_cmd(method::kVertexBuffer, 2, vb);
    const u32 ib[2] = {0x3000u, 0u};
    put_cmd(method::kIndexBuffer, 2, ib);
    const u32 stride = 16;
    put_cmd(method::kVertexStride, 1, &stride);
    const u32 ni = 3;
    put_cmd(method::kDrawElements, 1, &ni);

    (void)Rsx::process(16);
    const bool ok = (g_fb[30 * kFbW + 30] == 0x00808080u) && (Rsx::primitives_drawn() == 1);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX draw-elements test: %s prims=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(Rsx::primitives_drawn()));
}

void test_depth_test() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 depth_on = 1;
    put_cmd(method::kDepthEnable, 1, &depth_on);
    const u32 zclear = 0xFFFFFFFFu;
    put_cmd(method::kDepthClear, 1, &zclear);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 far_v0[4] = {10u, 10u, 100u, 0x00FF0000u};
    const u32 far_v1[4] = {110u, 10u, 100u, 0x00FF0000u};
    const u32 far_v2[4] = {10u, 110u, 100u, 0x00FF0000u};
    put_cmd(method::kVertexPush, 4, far_v0);
    put_cmd(method::kVertexPush, 4, far_v1);
    put_cmd(method::kVertexPush, 4, far_v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    const u32 near_v0[4] = {10u, 10u, 50u, 0x000000FFu};
    const u32 near_v1[4] = {110u, 10u, 50u, 0x000000FFu};
    const u32 near_v2[4] = {10u, 110u, 50u, 0x000000FFu};
    put_cmd(method::kVertexPush, 4, near_v0);
    put_cmd(method::kVertexPush, 4, near_v1);
    put_cmd(method::kVertexPush, 4, near_v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(32);
    const u32 px = g_fb[30 * kFbW + 30];
    const bool ok = (px == 0x000000FFu);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX depth test: %s pixel=0x%llx (expected 0xff)", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(px));
}

// ---------------------------------------------------------------------------
// 6D: smooth shading (with corrected barycentric weights)
//
// Triangle (10,10) red, (110,10) green, (10,110) blue.
// Point (30,30) has barycentric weights (0.6, 0.2, 0.2) -> (153, 51, 51).
// ---------------------------------------------------------------------------

void test_smooth_shading() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    const u32 smooth = 1;
    put_cmd(method::kInterpMode, 1, &smooth);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 v0[4] = {10u, 10u, 0u, 0x00FF0000u};
    const u32 v1[4] = {110u, 10u, 0u, 0x0000FF00u};
    const u32 v2[4] = {10u, 110u, 0u, 0x000000FFu};
    put_cmd(method::kVertexPush, 4, v0);
    put_cmd(method::kVertexPush, 4, v1);
    put_cmd(method::kVertexPush, 4, v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(16);

    const u32 px = g_fb[30 * kFbW + 30];
    // Integer division: 255 * 6000 / 10000 = 153 (0x99)
    //                   255 * 2000 / 10000 =  51 (0x33)
    const bool ok = (px == 0x00993333u);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX smooth-shading test: %s px=0x%llx (expected 0x993333)", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(px));
}

// ---------------------------------------------------------------------------
// 6D/6E: texture bind with per-vertex UVs (strict)
//
// 2x2 texture: (0,0)=red (1,0)=green (0,1)=blue (1,1)=white.
// Triangle corners receive UV (0,0), (32,0), (0,32) -- the full texture.
// Sample near corner a => red; near corner b => green.
// ---------------------------------------------------------------------------

void setup_tex_2x2() noexcept
{
    write_guest_u32(0x6000u + 0u, 0x00FF0000u);
    write_guest_u32(0x6000u + 4u, 0x0000FF00u);
    write_guest_u32(0x6000u + 8u, 0x000000FFu);
    write_guest_u32(0x6000u + 12u, 0x00FFFFFFu);
}

void bind_tex_2x2() noexcept
{
    const u32 bind[4] = {0x6000u, 0u, 2u, 2u};
    put_cmd(method::kTextureBind, 4, bind);
    const u32 enable = 1;
    put_cmd(method::kTextureEnable, 1, &enable);
    const u32 repeat = 0;
    put_cmd(method::kTextureWrap, 1, &repeat);
}

void test_texture_uv_corners() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    Rsx::set_guest_read32(&guest_read32, nullptr);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    setup_tex_2x2();
    bind_tex_2x2();

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    // Large triangle so the UV spread is clearly visible.
    const u32 v0[4] = {10u, 10u, 0u, 0x00FFFFFFu};
    const u32 v1[4] = {110u, 10u, 0u, 0x00FFFFFFu};
    const u32 v2[4] = {10u, 110u, 0u, 0x00FFFFFFu};

    const u32 uv0[2] = {0u, 0u};  // top-left of texture
    const u32 uv1[2] = {32u, 0u}; // top-right
    const u32 uv2[2] = {0u, 32u}; // bottom-left

    put_cmd(method::kVertexUV, 2, uv0);
    put_cmd(method::kVertexPush, 4, v0);
    put_cmd(method::kVertexUV, 2, uv1);
    put_cmd(method::kVertexPush, 4, v1);
    put_cmd(method::kVertexUV, 2, uv2);
    put_cmd(method::kVertexPush, 4, v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(32);

    // Near a (UV ~ 0,0) -> red texel.
    // Near b (UV ~ 32,0) -> green texel.
    const u32 near_a = g_fb[15 * kFbW + 15];
    const u32 near_b = g_fb[15 * kFbW + 100];
    const bool ok = (near_a == 0x00FF0000u) && (near_b == 0x0000FF00u);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX UV test: %s near_a=0x%llx near_b=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(near_a), static_cast<unsigned long long>(near_b));
}

// ---------------------------------------------------------------------------
// 6E: clamp wrap mode.
//
// With UV clamped, a UV outside the texture sample range maps to the
// boundary texel. Repeat vs clamp must produce different results.
// ---------------------------------------------------------------------------

void test_texture_wrap_clamp() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    Rsx::set_guest_read32(&guest_read32, nullptr);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    setup_tex_2x2();

    const u32 bind[4] = {0x6000u, 0u, 2u, 2u};
    put_cmd(method::kTextureBind, 4, bind);
    const u32 enable = 1;
    put_cmd(method::kTextureEnable, 1, &enable);
    const u32 clamp = 1;
    put_cmd(method::kTextureWrap, 1, &clamp);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    // UV goes to 4x the texture size. Clamp should cap at the top-right
    // texel; without clamping it would wrap. Sample at vertex v1 => the
    // boundary texel (1, 0) = green.
    const u32 v0[4] = {10u, 10u, 0u, 0x00FFFFFFu};
    const u32 v1[4] = {110u, 10u, 0u, 0x00FFFFFFu};
    const u32 v2[4] = {10u, 110u, 0u, 0x00FFFFFFu};

    const u32 uv0[2] = {0u, 0u};
    const u32 uv1[2] = {128u, 0u}; // 8 pixels -> way past 2x2 with clamp
    const u32 uv2[2] = {0u, 128u};

    put_cmd(method::kVertexUV, 2, uv0);
    put_cmd(method::kVertexPush, 4, v0);
    put_cmd(method::kVertexUV, 2, uv1);
    put_cmd(method::kVertexPush, 4, v1);
    put_cmd(method::kVertexUV, 2, uv2);
    put_cmd(method::kVertexPush, 4, v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(32);

    // Sample near v1 (which has UV (128,0)). Under clamp, UV clamps to
    // texel (1, 0) = green everywhere along the right edge.
    const u32 near_b = g_fb[15 * kFbW + 100];
    const bool ok = (near_b == 0x0000FF00u);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX clamp-wrap test: %s near_b=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(near_b));
}

// 6F: perspective-correct UVs. Two vertices at the same UV but different
// W produce different sampled texels because UV/W is interpolated in
// perspective-correct space and then divided by 1/W.
void test_perspective_uv() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    Rsx::set_guest_read32(&guest_read32, nullptr);

    const u32 black = 0, dummy = 0;
    put_cmd(method::kClearColor, 1, &black);
    put_cmd(method::kClear, 1, &dummy);

    // 8x8 texture with a red left half and green right half, so texel
    // sampling at the midpoint is decisive.
    for (u32 y = 0; y < 8; ++y)
        for (u32 x = 0; x < 8; ++x)
        {
            const u32 c = (x < 4u) ? 0x00FF0000u : 0x0000FF00u;
            write_guest_u32(0x7000u + (y * 8u + x) * 4u, c);
        }
    const u32 bind[4] = {0x7000u, 0u, 8u, 8u};
    put_cmd(method::kTextureBind, 4, bind);
    const u32 enable = 1;
    put_cmd(method::kTextureEnable, 1, &enable);
    const u32 repeat = 0;
    put_cmd(method::kTextureWrap, 1, &repeat);

    const u32 prim = static_cast<u32>(Primitive::Triangles);
    put_cmd(method::kPrimType, 1, &prim);

    const u32 v0[4] = {10u, 10u, 0u, 0x00FFFFFFu};
    const u32 v1[4] = {110u, 10u, 0u, 0x00FFFFFFu};
    const u32 v2[4] = {10u, 110u, 0u, 0x00FFFFFFu};

    // UV: v0 at left edge (0), v1 at right edge (128 => 8 pixels), v2 at
    // bottom-left. W: v0=1.0, v1=4.0, v2=1.0 (16.16).
    const u32 uv0[2] = {0u, 0u};
    const u32 uv1[2] = {128u, 0u};
    const u32 uv2[2] = {0u, 128u};
    const u32 w0[1] = {0x10000u};
    const u32 w1[1] = {0x40000u};
    const u32 w2[1] = {0x10000u};

    put_cmd(method::kVertexUV, 2, uv0);
    put_cmd(method::kVertexW, 1, w0);
    put_cmd(method::kVertexPush, 4, v0);
    put_cmd(method::kVertexUV, 2, uv1);
    put_cmd(method::kVertexW, 1, w1);
    put_cmd(method::kVertexPush, 4, v1);
    put_cmd(method::kVertexUV, 2, uv2);
    put_cmd(method::kVertexW, 1, w2);
    put_cmd(method::kVertexPush, 4, v2);
    put_cmd(method::kVertexFlush, 1, &dummy);

    (void)Rsx::process(32);

    // Sample near v1. Without perspective correction the UV at that
    // point would land exactly on the red/green boundary. With W = 4,
    // the effective U shrinks toward the origin, staying red.
    const u32 near_v1 = g_fb[15 * kFbW + 100];
    const bool ok = (near_v1 == 0x00FF0000u);
    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX perspective-UV test: %s near_v1=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(near_v1));
}

// 6F: mip chain generation.
void test_mip_chain() noexcept
{
    Rsx::init();
    Rsx::bind_surface_memory(g_fb, kFbW, kFbH, kFbW);
    Rsx::set_guest_read32(&guest_read32, nullptr);

    // 8x8 texture.
    for (u32 y = 0; y < 8; ++y)
        for (u32 x = 0; x < 8; ++x)
            write_guest_u32(0x8000u + (y * 8u + x) * 4u, 0x00AABBCCu);

    const u32 bind[4] = {0x8000u, 0u, 8u, 8u};
    put_cmd(method::kTextureBind, 4, bind);

    (void)Rsx::process(4);

    const bool ok = (Rsx::mip_levels() >= 4u) // 8 -> 4 -> 2 -> 1
                    && (Rsx::mip_width(0) == 8u) && (Rsx::mip_height(0) == 8u) &&
                    (Rsx::mip_width(1) == 4u) && (Rsx::mip_height(1) == 4u) &&
                    (Rsx::mip_width(2) == 2u) && (Rsx::mip_height(2) == 2u);

    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
               "RSX mip-chain test: %s levels=%llu 8x8->%llux%llu->%llux%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(Rsx::mip_levels()),
               static_cast<unsigned long long>(Rsx::mip_width(1)),
               static_cast<unsigned long long>(Rsx::mip_height(1)),
               static_cast<unsigned long long>(Rsx::mip_width(2)),
               static_cast<unsigned long long>(Rsx::mip_height(2)));
}

} // namespace

void self_test() noexcept
{
    if (!Rsx::ready())
    {
        log::write(log::Level::Warn, "rsx", "self-test skipped");
        return;
    }

    test_surface_pipeline();
    test_fifo_jump();
    test_fifo_call_return();
    test_unknown_tolerance();

    test_render_clear();
    test_render_triangle();
    test_render_tristrip();
    test_render_line();
    test_render_points();
    test_render_fan();

    test_draw_arrays();
    test_draw_elements();
    test_depth_test();

    test_smooth_shading();
    test_texture_uv_corners();
    test_texture_wrap_clamp();
    test_perspective_uv();
    test_mip_chain();

    log::write(log::Level::Info, "rsx",
               "RSX stats: cmds=%llu draws=%llu presents=%llu clears=%llu unknowns=%llu prims=%llu "
               "px=%llu",
               static_cast<unsigned long long>(Rsx::commands()),
               static_cast<unsigned long long>(Rsx::draws()),
               static_cast<unsigned long long>(Rsx::presents()),
               static_cast<unsigned long long>(Rsx::clears()),
               static_cast<unsigned long long>(Rsx::unknowns()),
               static_cast<unsigned long long>(Rsx::primitives_drawn()),
               static_cast<unsigned long long>(Rsx::pixels_written()));
}

} // namespace notyvos::ps3::rsx
