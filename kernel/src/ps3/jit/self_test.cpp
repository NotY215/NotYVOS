#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/exec_page.hpp>
#include <kernel/ps3/jit/cache.hpp>
#include <kernel/ps3/jit/jit.hpp>
#include <kernel/ps3/jit/self_test.hpp>

namespace notyvos::ps3::jit
{

namespace
{
// Scratch PPU RAM for the JIT self-test. Big-endian instructions.
alignas(64) u8 g_ram[4096];

bool ram_read32(void* user, u64 addr, u32* out) noexcept
{
    (void)user;
    if (addr + 4 > sizeof(g_ram))
        return false;
    const u8* p = g_ram + addr;
    *out = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    return true;
}

bool ram_write32(void* user, u64 addr, u32 value) noexcept
{
    (void)user;
    if (addr + 4 > sizeof(g_ram))
        return false;
    u8* p = g_ram + addr;
    p[0] = static_cast<u8>((value >> 24) & 0xFFu);
    p[1] = static_cast<u8>((value >> 16) & 0xFFu);
    p[2] = static_cast<u8>((value >> 8) & 0xFFu);
    p[3] = static_cast<u8>(value & 0xFFu);
    return true;
}

void put_insn(u64 pc, u32 word) noexcept
{
    (void)ram_write32(nullptr, pc, word);
}

} // namespace

void self_test() noexcept
{
    if (mm::ExecArena::capacity_bytes() == 0)
    {
        log::write(log::Level::Warn, "jit", "self-test skipped: no exec arena");
        return;
    }

    // Program:
    //   0x0000: addi r3, r0, 10        (0x3860000A)
    //   0x0004: addi r4, r0, 20        (0x38800014)
    //   0x0008: add  r5, r3, r4        (0x7CA32214)
    //   0x000C: b .                    (0x48000000)  -- infinite loop
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x0000, 0x3860000Au);
    put_insn(0x0004, 0x38800014u);
    put_insn(0x0008, 0x7CA32214u);
    put_insn(0x000C, 0x48000000u);

    ppu::Context ctx{};
    ppu::init(&ctx);
    ctx.read32 = ram_read32;
    ctx.write32 = ram_write32;
    ctx.pc = 0;

    TranslationCache::flush();

    // Fully qualified to avoid ADL pulling in notyvos::ps3::ppu::run.
    const u64 executed = notyvos::ps3::jit::run(&ctx, 4);

    const bool r3_ok = (ctx.gpr[3] == 10);
    const bool r4_ok = (ctx.gpr[4] == 20);
    const bool r5_ok = (ctx.gpr[5] == 30);
    const bool step_ok = (executed == 4);
    const bool cache_ok = (TranslationCache::block_count() >= 2);

    const bool ok = r3_ok && r4_ok && r5_ok && step_ok && cache_ok;

    log::write(
        ok ? log::Level::Info : log::Level::Warn, "jit",
        "JIT self-test: steps=%llu r3=%llu r4=%llu r5=%llu blocks=%llu cache_hits=%llu "
        "cache_misses=%llu fallback=%llu",
        static_cast<unsigned long long>(executed), static_cast<unsigned long long>(ctx.gpr[3]),
        static_cast<unsigned long long>(ctx.gpr[4]), static_cast<unsigned long long>(ctx.gpr[5]),
        static_cast<unsigned long long>(TranslationCache::block_count()),
        static_cast<unsigned long long>(TranslationCache::hits()),
        static_cast<unsigned long long>(TranslationCache::misses()),
        static_cast<unsigned long long>(fallback_steps()));

    log::write(ok ? log::Level::Info : log::Level::Warn, "jit", "JIT self-test: %s",
               ok ? "PASS" : "FAIL");
}

} // namespace notyvos::ps3::jit
