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
alignas(64) u8 g_ram[4096];

bool ram_read8(void* u, u64 a, u8* o) noexcept
{
    (void)u;
    if (a + 1 > sizeof(g_ram))
        return false;
    *o = g_ram[a];
    return true;
}
bool ram_read16(void* u, u64 a, u16* o) noexcept
{
    (void)u;
    if (a + 2 > sizeof(g_ram))
        return false;
    *o = static_cast<u16>((static_cast<u32>(g_ram[a]) << 8) | static_cast<u32>(g_ram[a + 1]));
    return true;
}
bool ram_read32(void* u, u64 a, u32* o) noexcept
{
    (void)u;
    if (a + 4 > sizeof(g_ram))
        return false;
    *o = (static_cast<u32>(g_ram[a]) << 24) | (static_cast<u32>(g_ram[a + 1]) << 16) |
         (static_cast<u32>(g_ram[a + 2]) << 8) | static_cast<u32>(g_ram[a + 3]);
    return true;
}
bool ram_read64(void* u, u64 a, u64* o) noexcept
{
    (void)u;
    if (a + 8 > sizeof(g_ram))
        return false;
    u64 v = 0;
    for (u32 i = 0; i < 8; ++i)
        v = (v << 8) | g_ram[a + i];
    *o = v;
    return true;
}

bool ram_write8(void* u, u64 a, u8 v) noexcept
{
    (void)u;
    if (a + 1 > sizeof(g_ram))
        return false;
    g_ram[a] = v;
    return true;
}
bool ram_write16(void* u, u64 a, u16 v) noexcept
{
    (void)u;
    if (a + 2 > sizeof(g_ram))
        return false;
    g_ram[a] = static_cast<u8>((v >> 8) & 0xFFu);
    g_ram[a + 1] = static_cast<u8>(v & 0xFFu);
    return true;
}

bool ram_write32(void* u, u64 a, u32 v) noexcept
{
    (void)u;
    if (a + 4 > sizeof(g_ram))
        return false;
    g_ram[a] = static_cast<u8>((v >> 24) & 0xFFu);
    g_ram[a + 1] = static_cast<u8>((v >> 16) & 0xFFu);
    g_ram[a + 2] = static_cast<u8>((v >> 8) & 0xFFu);
    g_ram[a + 3] = static_cast<u8>(v & 0xFFu);
    return true;
}

bool ram_write64(void* u, u64 a, u64 v) noexcept
{
    (void)u;
    if (a + 8 > sizeof(g_ram))
        return false;
    for (u32 i = 0; i < 8; ++i)
        g_ram[a + i] = static_cast<u8>((v >> ((7u - i) * 8u)) & 0xFFu);
    return true;
}

void put_insn(u64 pc, u32 word) noexcept
{
    (void)ram_write32(nullptr, pc, word);
}

void wire_ram(ppu::Context& c) noexcept
{
    c.read8 = ram_read8;
    c.read16 = ram_read16;
    c.read32 = ram_read32;
    c.read64 = ram_read64;
    c.write8 = ram_write8;
    c.write16 = ram_write16;
    c.write32 = ram_write32;
    c.write64 = ram_write64;
}

void test_arith() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3860000Au); // addi r3, r0, 10
    put_insn(0x04, 0x38800014u); // addi r4, r0, 20
    put_insn(0x08, 0x7CA32214u); // add  r5, r3, r4   -> r5 = 30
    put_insn(0x0C, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    const u64 ex = notyvos::ps3::jit::run(&ctx, 4);

    const bool ok = (ctx.gpr[3] == 10) && (ctx.gpr[4] == 20) && (ctx.gpr[5] == 30) && (ex == 4);
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT arith  test: %s steps=%llu r3=%llu r4=%llu r5=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ex), static_cast<unsigned long long>(ctx.gpr[3]),
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

void test_memory() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x38600064u); // addi r3, r0, 100
    put_insn(0x04, 0x90600100u); // stw  r3, 0x100(r0)
    put_insn(0x08, 0x80800100u); // lwz  r4, 0x100(r0)
    put_insn(0x0C, 0x7CA32214u); // add  r5, r3, r4   -> r5 = 200
    put_insn(0x10, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    const u64 ex = notyvos::ps3::jit::run(&ctx, 5);

    const bool ok = (ctx.gpr[3] == 100) && (ctx.gpr[4] == 100) && (ctx.gpr[5] == 200) && (ex == 5);
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT memory test: %s steps=%llu r3=%llu r4=%llu r5=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ex), static_cast<unsigned long long>(ctx.gpr[3]),
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

void test_branch() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x38600003u); // addi r3, r0, 3
    // mtspr 9, r3  (CTR = r3)
    const u32 mtspr = (31u << 26) | (3u << 21) | (9u << 16) | (467u << 1);
    put_insn(0x04, mtspr);
    put_insn(0x08, 0x38800000u); // addi r4, r0, 0
    put_insn(0x0C, 0x38840001u); // addi r4, r4, 1
    put_insn(0x10, 0x4200FFFCu); // bc 16,0,-4 (bdnz 0x0C)
    put_insn(0x14, 0x38A00063u); // addi r5, r0, 99
    put_insn(0x18, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    const u64 ex = notyvos::ps3::jit::run(&ctx, 32);

    const bool ok = (ctx.gpr[4] == 3) && (ctx.gpr[5] == 99) && (ctx.ctr == 0);
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT branch test: %s steps=%llu r4=%llu r5=%llu ctr=%llu", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ex), static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]),
               static_cast<unsigned long long>(ctx.ctr));
}

// 5C: extsb + extsh + srawi
void test_extend_shift() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    // r3 = 0xFFFFFF80 (128 with sign flipped in low byte)
    // Not via addi (16-bit); build with addis+ori: r3 = 0xFFFF8000
    put_insn(0x00, 0x3C60FFFFu); // addis r3, r0, -1   -> r3 = 0xFFFFFFFFFFFF0000
    put_insn(0x04, 0x60638000u); // ori   r3, r3, 0x8000 -> r3 = 0xFFFFFFFFFFFF8000
    // extsb r4, r3  -> low byte 0x80 -> sign-extended to 0xFFFFFFFFFFFFFF80
    // extsb: primary 31, XO 954, rt=rS(3), ra=rA(4)
    const u32 extsb_w = (31u << 26) | (3u << 21) | (4u << 16) | (954u << 1);
    put_insn(0x08, extsb_w);
    // srawi r5, r4, 4  -> arithmetic shift right (r4 is -128, /16 = -8)
    const u32 srawi_w = (31u << 26) | (4u << 21) | (5u << 16) | (4u << 11) | (824u << 1);
    put_insn(0x0C, srawi_w);
    put_insn(0x10, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 5);

    // r4 = 0xFFFFFFFFFFFFFF80 (sign-extended 0x80)
    // r5 = (i32)(r4 low 32) >> 4, zero-extended:
    //      low32 of r4 = 0xFFFFFF80 = -128 (i32)
    //      >> 4 = -8 = 0xFFFFFFF8
    //      zero-extended to 64: 0x00000000FFFFFFF8
    const bool r4_ok = (ctx.gpr[4] == 0xFFFFFFFFFFFFFF80ULL);
    const bool r5_ok = (ctx.gpr[5] == 0x00000000FFFFFFF8ULL);
    const bool ok = r4_ok && r5_ok;

    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT extend/shift test: %s r4=0x%llx r5=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

// 5C: ld / std
void test_ld_std() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    // r3 = 0xDEADBEEFCAFEBABE via addis+ori sequence
    // Build high half via addis, low via ori, then shift left 32 and or.
    // Easier: store the constant in memory and ld it back? No, ld reads memory.
    // So we need the constant in a register. Use two 16-bit immediate ops:
    //   addis r3, r0, 0xDEAD   -> r3 = sign_ext(0xDEAD)<<16 = 0xFFFFFFFFDEAD0000
    //   ori   r3, r3, 0xBEEF   -> r3 = 0xFFFFFFFFDEADBEEF
    // To build the full 64-bit value, we'd need shift-left 32. Not available.
    // Simplify: use a value whose sign-extended upper half is acceptable.
    put_insn(0x00, 0x3C60DEADu); // addis r3, r0, 0xDEAD  (=> r3 = 0xFFFFFFFFDEAD0000)
    put_insn(0x04, 0x6063BEEFu); // ori   r3, r3, 0xBEEF  (=> r3 = 0xFFFFFFFFDEADBEEF)
    // std r3, 0x200(r0)  : DS-form, primary 62, XO=0, DS=0x200
    // word = (62<<26) | (3<<21) | (0<<16) | (0x200 & 0xFFFC) | 0
    put_insn(0x08, 0xF8600200u);
    // ld  r4, 0x200(r0)  : DS-form, primary 58, XO=0, DS=0x200
    put_insn(0x0C, 0xE8800200u);
    // add r5, r3, r4  -> 2 * r3
    put_insn(0x10, 0x7CA32214u);
    put_insn(0x14, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 6);

    const u64 expected = 0xFFFFFFFFDEADBEEFULL;
    const bool r3_ok = (ctx.gpr[3] == expected);
    const bool r4_ok = (ctx.gpr[4] == expected);
    const bool r5_ok = (ctx.gpr[5] == (expected + expected));
    const bool ok = r3_ok && r4_ok && r5_ok;

    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT ld/std test: %s r3=0x%llx r4=0x%llx r5=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[3]),
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

} // namespace

void self_test() noexcept
{
    if (mm::ExecArena::capacity_bytes() == 0)
    {
        log::write(log::Level::Warn, "jit", "self-test skipped: no exec arena");
        return;
    }

    test_arith();
    test_memory();
    test_branch();
    test_extend_shift();
    test_ld_std();

    log::write(log::Level::Info, "jit",
               "JIT stats: blocks=%llu translated=%llu entered=%llu fallback=%llu faults=%llu",
               static_cast<unsigned long long>(TranslationCache::block_count()),
               static_cast<unsigned long long>(blocks_translated()),
               static_cast<unsigned long long>(blocks_entered()),
               static_cast<unsigned long long>(fallback_steps()),
               static_cast<unsigned long long>(fault_count()));
}

} // namespace notyvos::ps3::jit
