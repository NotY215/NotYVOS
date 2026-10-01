#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/rsx/rsx.hpp>
#include <kernel/ps3/rsx/self_test.hpp>

namespace notyvos::ps3::rsx
{

namespace
{

// Build a FIFO header word for a method at `byte_offset` with `count`
// data words (1..4).
constexpr u32 header(u32 byte_offset, u32 count) noexcept
{
    return ((byte_offset >> 2) & 0x3FFFu)
         | ((count - 1u) & 0x3u) << 16;
}

void put_cmd(u32 byte_offset, u32 count, const u32* data) noexcept
{
    (void)Rsx::push(header(byte_offset, count));
    for (u32 i = 0; i < count; ++i)
        (void)Rsx::push(data[i]);
}

// ---------------------------------------------------------------------------
// Test 1: surface setup, clear, draw, present.
// ---------------------------------------------------------------------------
void test_surface_pipeline() noexcept
{
    Rsx::init();

    const u32 fmt      = static_cast<u32>(SurfaceFormat::A8R8G8B8);
    const u32 col_lo   = 0x00100000u;      // 1 MiB
    const u32 col_hi   = 0x00000000u;
    const u32 pitch    = 800u * 4u;
    const u32 width    = 800u;
    const u32 height   = 600u;
    const u32 clear_rgba[4] = { 0x00000000u, 0x00000000u, 0x00000000u, 0x00FF0000u };

    put_cmd(method::kSurfaceFmt, 1, &fmt);
    const u32 col_words[2] = { col_lo, col_hi };
    put_cmd(method::kSurfaceCol, 2, col_words);
    put_cmd(method::kSurfacePit, 1, &pitch);
    put_cmd(method::kSurfaceW,   1, &width);
    put_cmd(method::kSurfaceH,   1, &height);
    put_cmd(method::kClearColor, 4, clear_rgba);
    put_cmd(method::kClear,      1, &width); // value ignored by handler
    put_cmd(method::kDraw,       1, &width);
    put_cmd(method::kPresent,    1, &width);

    const u32 done = Rsx::process(32);

    const Surface& s = Rsx::target();
    const bool surf_ok = s.valid
                      && s.format == SurfaceFormat::A8R8G8B8
                      && s.address == 0x100000u
                      && s.pitch == 3200u
                      && s.width == 800u
                      && s.height == 600u;

    const bool stats_ok = (Rsx::commands() >= 9) && (Rsx::clears() == 1)
                       && (Rsx::draws() == 1) && (Rsx::presents() == 1)
                       && (Rsx::unknowns() == 0);

    const bool ok = (done >= 9) && surf_ok && stats_ok;

    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
        "RSX surface test: %s cmds=%llu clear=%llu draw=%llu present=%llu surf=[0x%llx %llux%llu p=%llu fmt=%llu]",
        ok ? "PASS" : "FAIL",
        static_cast<unsigned long long>(Rsx::commands()),
        static_cast<unsigned long long>(Rsx::clears()),
        static_cast<unsigned long long>(Rsx::draws()),
        static_cast<unsigned long long>(Rsx::presents()),
        static_cast<unsigned long long>(s.address),
        static_cast<unsigned long long>(s.width),
        static_cast<unsigned long long>(s.height),
        static_cast<unsigned long long>(s.pitch),
        static_cast<unsigned long long>(static_cast<u32>(s.format)));
}

// ---------------------------------------------------------------------------
// Test 2: FIFO control flow (jump).
//
// Layout in the FIFO:
//   [0] NOP              (byte 0x0100)
//   [1] JUMP to index 4
//   [2] value 0xDEADBEEF
//   [3] (unreached NOP)
//   [4] DRAW             (byte 0x0224)
//   [5] value 0
// ---------------------------------------------------------------------------
void test_fifo_jump() noexcept
{
    Rsx::init();

    const u32 zero = 0;

    // index 0
    put_cmd(method::kNop, 1, &zero);
    // index 2 (after NOP): JUMP to word index 4
    const u32 jump_target = 4;
    put_cmd(method::kFifoJump, 1, &jump_target);
    // index 4..5 (unreachable garbage in stream but skipped by jump)
    const u32 poison = 0xDEADBEEFu;
    put_cmd(method::kNop, 1, &poison);
    // index 6..7: DRAW
    put_cmd(method::kDraw, 1, &zero);

    const u64 before_draws = Rsx::draws();
    (void)Rsx::process(8);

    const bool ok = (Rsx::draws() == before_draws + 1)
                 && (Rsx::commands() >= 3);

    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
        "RSX FIFO jump test: %s draws=%llu cmds=%llu",
        ok ? "PASS" : "FAIL",
        static_cast<unsigned long long>(Rsx::draws()),
        static_cast<unsigned long long>(Rsx::commands()));
}

// ---------------------------------------------------------------------------
// Test 3: FIFO call / return.
//
// Stream:
//   main[0] : CALL sub
//   main[2] : PRESENT
//   main[4] : (end)
//   sub [6]: DRAW
//   sub [8]: RETURN
// ---------------------------------------------------------------------------
void test_fifo_call_return() noexcept
{
    Rsx::init();

    const u32 zero = 0;
    const u32 sub_target = 6;   // word index

    // Push main body first.
    put_cmd(method::kFifoCall, 1, &sub_target);
    put_cmd(method::kPresent,  1, &zero);
    // Subroutine body (would be reached via CALL).
    put_cmd(method::kDraw,     1, &zero);
    put_cmd(method::kFifoReturn, 1, &zero);

    const u32 r = Rsx::process(16);
    const bool ok = (Rsx::draws() == 1) && (Rsx::presents() == 1) && (r >= 4);

    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
        "RSX FIFO call/return test: %s draws=%llu presents=%llu processed=%llu",
        ok ? "PASS" : "FAIL",
        static_cast<unsigned long long>(Rsx::draws()),
        static_cast<unsigned long long>(Rsx::presents()),
        static_cast<unsigned long long>(r));
}

// ---------------------------------------------------------------------------
// Test 4: unknown method tolerance.
//
// Emit a command whose method has no handler. The dispatcher must log it
// once (rate-limited), increment the counter, and continue processing
// subsequent known commands.
// ---------------------------------------------------------------------------
void test_unknown_tolerance() noexcept
{
    Rsx::init();

    const u32 zero = 0;
    constexpr u32 kBogus = 0x0FFC;   // inside the register space, unmapped

    put_cmd(kBogus, 1, &zero);
    put_cmd(method::kPresent, 1, &zero);

    (void)Rsx::process(4);

    const bool ok = (Rsx::unknowns() == 1) && (Rsx::presents() == 1);

    log::write(ok ? log::Level::Info : log::Level::Warn, "rsx",
        "RSX unknown-method test: %s unknowns=%llu presents=%llu",
        ok ? "PASS" : "FAIL",
        static_cast<unsigned long long>(Rsx::unknowns()),
        static_cast<unsigned long long>(Rsx::presents()));
}

} // namespace

void self_test() noexcept
{
    if (!Rsx::ready())
    {
        log::write(log::Level::Warn, "rsx", "self-test skipped: RSX not initialized");
        return;
    }

    test_surface_pipeline();
    test_fifo_jump();
    test_fifo_call_return();
    test_unknown_tolerance();

    log::write(log::Level::Info, "rsx",
        "RSX stats: cmds=%llu draws=%llu presents=%llu clears=%llu unknowns=%llu",
        static_cast<unsigned long long>(Rsx::commands()),
        static_cast<unsigned long long>(Rsx::draws()),
        static_cast<unsigned long long>(Rsx::presents()),
        static_cast<unsigned long long>(Rsx::clears()),
        static_cast<unsigned long long>(Rsx::unknowns()));
}

} // namespace notyvos::ps3::rsx
