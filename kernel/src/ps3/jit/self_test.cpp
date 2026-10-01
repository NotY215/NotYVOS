#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/exec_page.hpp>
#include <kernel/ps3/jit/cache.hpp>
#include <kernel/ps3/jit/jit.hpp>
#include <kernel/ps3/jit/self_test.hpp>

// Declared in translate.cpp with extern "C" linkage.
extern "C" bool notyvos_jit_store32(notyvos::ps3::ppu::Context* ctx, notyvos::u64 ea,
                                    notyvos::u64 value) noexcept;

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

// ---------------------------------------------------------------------------
// Test 1: integer arithmetic (5A).
// ---------------------------------------------------------------------------
void test_arith() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3860000Au); // addi r3, r0, 10
    put_insn(0x04, 0x38800014u); // addi r4, r0, 20
    put_insn(0x08, 0x7CA32214u); // add  r5, r3, r4   -> 30
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

// ---------------------------------------------------------------------------
// Test 2: D-form memory round-trip (5B).
// ---------------------------------------------------------------------------
void test_memory() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x38600064u); // addi r3, r0, 100
    put_insn(0x04, 0x90600100u); // stw  r3, 0x100(r0)
    put_insn(0x08, 0x80800100u); // lwz  r4, 0x100(r0)
    put_insn(0x0C, 0x7CA32214u); // add  r5, r3, r4   -> 200
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

// ---------------------------------------------------------------------------
// Test 3: bdnz loop (5B conditional branch).
// ---------------------------------------------------------------------------
void test_branch() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x38600003u); // addi r3, r0, 3
    const u32 mtspr = (31u << 26) | (3u << 21) | (9u << 16) | (467u << 1);
    put_insn(0x04, mtspr);       // mtspr 9, r3  (CTR = r3)
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

// ---------------------------------------------------------------------------
// Test 4: extsb + srawi (5C part 1).
//
// r3 = 0xFFFF8000 built via addis then ori low byte 0x80.
// extsb r4, r3  -> 0xFFFFFFFFFFFFFF80
// srawi r5, r4, 4 -> 0x00000000FFFFFFF8
// ---------------------------------------------------------------------------
void test_extend_shift() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3C60FFFFu); // addis r3, r0, -1
    put_insn(0x04, 0x60630080u); // ori   r3, r3, 0x0080
    const u32 extsb_w = (31u << 26) | (3u << 21) | (4u << 16) | (954u << 1);
    put_insn(0x08, extsb_w); // extsb r4, r3
    const u32 srawi_w = (31u << 26) | (4u << 21) | (5u << 16) | (4u << 11) | (824u << 1);
    put_insn(0x0C, srawi_w);     // srawi r5, r4, 4
    put_insn(0x10, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 5);

    const bool r4_ok = (ctx.gpr[4] == 0xFFFFFFFFFFFFFF80ULL);
    const bool r5_ok = (ctx.gpr[5] == 0x00000000FFFFFFF8ULL);
    const bool ok = r4_ok && r5_ok;

    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT extend/shift test: %s r4=0x%llx r5=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

// ---------------------------------------------------------------------------
// Test 5: ld / std (5C part 1, DS-form).
// ---------------------------------------------------------------------------
void test_ld_std() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3C60DEADu); // addis r3, r0, 0xDEAD  (sign-extended)
    put_insn(0x04, 0x6063BEEFu); // ori   r3, r3, 0xBEEF
    put_insn(0x08, 0xF8600200u); // std r3, 0x200(r0)
    put_insn(0x0C, 0xE8800200u); // ld  r4, 0x200(r0)
    put_insn(0x10, 0x7CA32214u); // add r5, r3, r4
    put_insn(0x14, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 6);

    const u64 expected = 0xFFFFFFFFDEADBEEFULL;
    const bool ok = (ctx.gpr[3] == expected) && (ctx.gpr[4] == expected) &&
                    (ctx.gpr[5] == (expected + expected));
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT ld/std test: %s r3=0x%llx r4=0x%llx r5=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[3]),
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

// ---------------------------------------------------------------------------
// Test 6: rlwinm (5C part 2).
//
// r3 = 0xFFFF5678 (via addis+ori)
// rlwinm r4, r3, 8, 0, 7   -> low32(r3) = 0xFFFF5678
//                             rotl32(x, 8) = 0xFF5678FF
//                             & 0x000000FF  = 0x000000FF
// ---------------------------------------------------------------------------
void test_rlwinm() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3C60FFFFu); // addis r3, r0, -1
    put_insn(0x04, 0x60635678u); // ori   r3, r3, 0x5678
    const u32 rlwinm_w = (21u << 26) | (3u << 21) | (4u << 16) | (8u << 11) | (0u << 6) | (7u << 1);
    put_insn(0x08, rlwinm_w);    // rlwinm r4, r3, 8, 0, 7
    put_insn(0x0C, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 3);

    const bool ok = (ctx.gpr[4] == 0x00000000000000FFULL);
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT rlwinm test: %s r4=0x%llx (expected 0xff)", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[4]));
}

// ---------------------------------------------------------------------------
// Test 7: rlwimi (5C part 2).
//
// r3 = 0xABCD1234
// r4 = 0x11223344
// rlwimi r4, r3, 4, 8, 15   -> r4[8:15] <- bits 8:15 of rotl32(r3, 4)
// ---------------------------------------------------------------------------
void test_rlwimi() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    // r3 = 0x00000000ABCD1234 (via addis+ori, sign-extended high)
    put_insn(0x00, 0x3C600000u); // addis r3, r0, 0
    put_insn(0x04, 0x60630000u); // ori r3, r3, 0    (clear)
    // easier: load the constant via ld from memory. Not available in test
    // ram without extra plumbing. Use addis+ori.
    put_insn(0x00, 0x3C60ABCDu); // addis r3, r0, 0xABCD  -> 0xFFFFFFFFABCD0000
    put_insn(0x04, 0x60631234u); // ori   r3, r3, 0x1234  -> 0xFFFFFFFFABCD1234
    put_insn(0x08, 0x3C801122u); // addis r4, r0, 0x1122
    put_insn(0x0C, 0x60843344u); // ori   r4, r4, 0x3344  -> 0xFFFFFFFF11223344

    // rlwimi r4, r3, 4, 8, 15
    //   word = 20<<26 | RS(3)<<21 | RA(4)<<16 | SH(4)<<11 | MB(8)<<6 | ME(15)<<1
    const u32 rlwimi_w =
        (20u << 26) | (3u << 21) | (4u << 16) | (4u << 11) | (8u << 6) | (15u << 1);
    put_insn(0x10, rlwimi_w);
    put_insn(0x14, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 6);

    // low32(r3) = 0xABCD1234
    // rotl32(0xABCD1234, 4) = 0xBCD1234A
    // bits 8:15 = 0x23
    // r4 result = (0x11223344 & ~0xFF00) | (0x23 << 8)
    //           = 0x11220044 | 0x2300 = 0x11222344
    const bool ok = (ctx.gpr[4] == 0x0000000011222344ULL);
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT rlwimi test: %s r4=0x%llx (expected 0x11222344)", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[4]));
}

// ---------------------------------------------------------------------------
// Test 8: rlwnm (5C part 2).
//
// r3 = 0x12345678
// r4 = 5
// rlwnm r5, r3, r4, 0, 7  ->  rotl32(r3, 5) & 0xFF
//                           = 0x2468ACF0 & 0xFF = 0xF0
// ---------------------------------------------------------------------------
void test_rlwnm() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3C601234u); // addis r3, r0, 0x1234
    put_insn(0x04, 0x60635678u); // ori   r3, r3, 0x5678
    put_insn(0x08, 0x38800005u); // addi  r4, r0, 5
    // rlwnm r5, r3, r4, 0, 7
    //   word = 23<<26 | RS(3)<<21 | RA(5)<<16 | RB(4)<<11 | MB(0)<<6 | ME(7)<<1
    const u32 rlwnm_w = (23u << 26) | (3u << 21) | (5u << 16) | (4u << 11) | (0u << 6) | (7u << 1);
    put_insn(0x0C, rlwnm_w);
    put_insn(0x10, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 4);

    // low32(r3) = 0x12345678
    // rotl32(0x12345678, 5) = (x << 5) | (x >> 27)
    //                       = 0x468ACF00 | 0x00000002 = 0x468ACF02
    // & 0xFF = 0x02
    const bool ok = (ctx.gpr[5] == 0x0000000000000002ULL);
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT rlwnm  test: %s r5=0x%llx (expected 0x2)", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[5]));
}

// ---------------------------------------------------------------------------
// Test 9: indexed load/store (5C part 2).
//
// r3 = 0xCAFEBABE12345678
// r4 = 0x100
// stwx r3, r0, r4       -> mem[0x100] = r3
// lwzx r5, r0, r4       -> r5 = low32(mem[0x100]) zero-ext
// stdx r3, r0, r4       -> mem[0x100] = r3 (8 bytes)
// ldx  r6, r0, r4       -> r6 = mem[0x100]
// ---------------------------------------------------------------------------
void test_indexed() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x3C60CAFEu); // addis r3, r0, 0xCAFE  -> 0xFFFFFFFFCAFE0000
    put_insn(0x04, 0x6063BABEu); // ori   r3, r3, 0xBABE  -> 0xFFFFFFFFCAFEBABE
    put_insn(0x08, 0x38800100u); // addi  r4, r0, 0x100
    // stwx r3, r0, r4
    const u32 stwx_w = (31u << 26) | (3u << 21) | (0u << 16) | (4u << 11) | (151u << 1);
    put_insn(0x0C, stwx_w);
    // lwzx r5, r0, r4
    const u32 lwzx_w = (31u << 26) | (5u << 21) | (0u << 16) | (4u << 11) | (23u << 1);
    put_insn(0x10, lwzx_w);
    // stdx r3, r0, r4
    const u32 stdx_w = (31u << 26) | (3u << 21) | (0u << 16) | (4u << 11) | (149u << 1);
    put_insn(0x14, stdx_w);
    // ldx r6, r0, r4
    const u32 ldx_w = (31u << 26) | (6u << 21) | (0u << 16) | (4u << 11) | (21u << 1);
    put_insn(0x18, ldx_w);
    put_insn(0x1C, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 8);

    const u64 expect = 0xFFFFFFFFCAFEBABEULL;
    const bool r5_ok = (ctx.gpr[5] == 0x00000000CAFEBABEULL); // lwzx: zero-ext low 32
    const bool r6_ok = (ctx.gpr[6] == expect);                // ldx: full 64-bit
    const bool ok = r5_ok && r6_ok;

    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT indexed test: %s r5=0x%llx r6=0x%llx", ok ? "PASS" : "FAIL",
               static_cast<unsigned long long>(ctx.gpr[5]),
               static_cast<unsigned long long>(ctx.gpr[6]));
}

// ---------------------------------------------------------------------------
// Test 10: SMC invalidation (5D).
//
// Translate a block, then write new code over it through the JIT store
// helper, and verify the block is invalidated and re-translated on the
// next execution.
// ---------------------------------------------------------------------------
void test_smc() noexcept
{
    libk::memset(g_ram, 0, sizeof(g_ram));
    put_insn(0x00, 0x38600007u); // addi r3, r0, 7
    put_insn(0x04, 0x48000000u); // b .

    ppu::Context ctx{};
    ppu::init(&ctx);
    wire_ram(ctx);
    ctx.pc = 0;
    TranslationCache::flush();
    (void)notyvos::ps3::jit::run(&ctx, 2);

    const bool first_ok = (ctx.gpr[3] == 7) && (TranslationCache::probe(0x00) != nullptr);

    // Overwrite the first insn through the JIT store helper (which
    // triggers notyvos_jit_smc_check and invalidates the cached block).
    (void)notyvos_jit_store32(&ctx, 0x00, 0x3860002Au); // addi r3, r0, 42
    const bool invalidated = (TranslationCache::probe(0x00) == nullptr);

    ctx.gpr[3] = 0;
    ctx.pc = 0;
    (void)notyvos::ps3::jit::run(&ctx, 2);
    const bool second_ok = (ctx.gpr[3] == 42);

    const bool ok = first_ok && invalidated && second_ok;
    log::write(ok ? log::Level::Info : log::Level::Warn, "jit",
               "JIT SMC test: %s first=%s invalidated=%s second=%s", ok ? "PASS" : "FAIL",
               first_ok ? "yes" : "no", invalidated ? "yes" : "no", second_ok ? "yes" : "no");
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
    test_rlwinm();
    test_rlwimi();
    test_rlwnm();
    test_indexed();
    test_smc();

    log::write(log::Level::Info, "jit",
               "JIT stats: blocks=%llu translated=%llu entered=%llu fallback=%llu faults=%llu",
               static_cast<unsigned long long>(TranslationCache::block_count()),
               static_cast<unsigned long long>(blocks_translated()),
               static_cast<unsigned long long>(blocks_entered()),
               static_cast<unsigned long long>(fallback_steps()),
               static_cast<unsigned long long>(fault_count()));
}

} // namespace notyvos::ps3::jit
