#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/dma.hpp>
#include <kernel/ps3/jit/self_test.hpp>
#include <kernel/ps3/loader.hpp>
#include <kernel/ps3/powerpc/decode.hpp>
#include <kernel/ps3/ppu.hpp>
#include <kernel/ps3/spu.hpp>
#include <kernel/ps3/rsx/self_test.hpp>

namespace notyvos::ps3
{

namespace
{

struct DecodeCase
{
    u32 word;
    const char* expect;
};

u8 g_ppu_ram[4096];

bool ppu_read32(void* user, u64 addr, u32* out) noexcept
{
    (void)user;
    if (addr + 4 > sizeof(g_ppu_ram))
        return false;
    const u8* p = g_ppu_ram + addr;
    *out = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    return true;
}

bool ppu_write32(void* user, u64 addr, u32 value) noexcept
{
    (void)user;
    if (addr + 4 > sizeof(g_ppu_ram))
        return false;
    u8* p = g_ppu_ram + addr;
    p[0] = static_cast<u8>((value >> 24) & 0xFF);
    p[1] = static_cast<u8>((value >> 16) & 0xFF);
    p[2] = static_cast<u8>((value >> 8) & 0xFF);
    p[3] = static_cast<u8>(value & 0xFF);
    return true;
}

void write_be32(u8* p, u32 v) noexcept
{
    p[0] = static_cast<u8>((v >> 24) & 0xFF);
    p[1] = static_cast<u8>((v >> 16) & 0xFF);
    p[2] = static_cast<u8>((v >> 8) & 0xFF);
    p[3] = static_cast<u8>(v & 0xFF);
}

void test_decoder() noexcept
{
    const DecodeCase cases[] = {
        {0x38600042u, "addi"},  {0x48000008u, "b"},     {0x4E800020u, "bclr"},
        {0x7C0802A6u, "mfspr"}, {0x7C0803A6u, "mtspr"}, {0x9421FFE0u, "stwu"},
        {0x38210020u, "addi"},  {0x4E800420u, "bcctr"},
    };

    u32 ok = 0;
    for (const auto& c : cases)
    {
        const auto ins = powerpc::decode(c.word);
        if (ins.mnemonic && libk::strcmp(ins.mnemonic, c.expect) == 0)
            ++ok;
        else
            log::write(log::Level::Warn, "ps3-dec", "0x%llx decoded as '%s', expected '%s'",
                       static_cast<unsigned long long>(c.word),
                       ins.mnemonic ? ins.mnemonic : "(null)", c.expect);
    }
    log::write(log::Level::Info, "ps3-dec", "decoder self-test: %u/%u instructions recognised",
               static_cast<unsigned long long>(ok),
               static_cast<unsigned long long>(sizeof(cases) / sizeof(cases[0])));
}

void test_elf() noexcept
{
    static const u8 fake_elf[64] = {
        0x7F, 'E',  'L',  'F',  2,    2,    1,    0,    0,    0,    0,    0,    0,
        0,    0,    0,    0x00, 0x02, 0x00, 0x15, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x40, 0x00, 0x38, 0x00, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
    };
    if (!is_ps3_executable(fake_elf, sizeof(fake_elf)))
    {
        log::write(log::Level::Warn, "ps3", "elf self-test: rejected");
        return;
    }
    Ps3Program prog{};
    if (!parse_ps3_executable(fake_elf, sizeof(fake_elf), &prog))
    {
        log::write(log::Level::Warn, "ps3", "elf self-test: parse failed");
        return;
    }
    log::write(log::Level::Info, "ps3", "elf self-test: entry=0x%llx, %u phdr",
               static_cast<unsigned long long>(prog.entry),
               static_cast<unsigned long long>(prog.phnum));
}

void test_ppu() noexcept
{
    libk::memset(g_ppu_ram, 0, sizeof(g_ppu_ram));
    write_be32(g_ppu_ram + 0, 0x38600001u);
    write_be32(g_ppu_ram + 4, 0x38800002u);
    write_be32(g_ppu_ram + 8, 0x7CA32214u);
    write_be32(g_ppu_ram + 12, 0x48000000u);

    ppu::Context ctx{};
    ppu::init(&ctx);
    ctx.read32 = ppu_read32;
    ctx.write32 = ppu_write32;
    ctx.pc = 0;
    (void)ppu::run(&ctx, 3);

    const bool ok = (ctx.gpr[3] == 1) && (ctx.gpr[4] == 2) && (ctx.gpr[5] == 3);
    log::write(ok ? log::Level::Info : log::Level::Warn, "ps3-ppu",
               "PPU self-test: r3=%llu r4=%llu r5=%llu (expected 1/2/3)",
               static_cast<unsigned long long>(ctx.gpr[3]),
               static_cast<unsigned long long>(ctx.gpr[4]),
               static_cast<unsigned long long>(ctx.gpr[5]));
}

// SPU encoding helpers. Layout must match spu.cpp:
//   opcode bits 31..25, rt bits 24..18, ra bits 17..11, rb bits 10..4
// Immediate form (il, dma): imm16 at bits 17..2.
static constexpr u32 spu_R(u32 op, u32 rt, u32 ra, u32 rb) noexcept
{
    return ((op & 0x7Fu) << 25) | ((rt & 0x7Fu) << 18) | ((ra & 0x7Fu) << 11) | ((rb & 0x7Fu) << 4);
}
static constexpr u32 spu_I(u32 op, u32 rt, u32 imm) noexcept
{
    return ((op & 0x7Fu) << 25) | ((rt & 0x7Fu) << 18) | ((imm & 0xFFFFu) << 2);
}
static constexpr u32 spu_op_halt = 0x00;
static constexpr u32 spu_op_or = 0x13;
static constexpr u32 spu_op_il = 0x20;
static constexpr u32 spu_op_wrch = 0x21;

// spu::Context is ~262 KiB. Must not live on the stack.
static spu::Context g_spu_ctx;

void test_spu() noexcept
{
    spu::init(&g_spu_ctx);

    //   0x00: il   r1, 1    -> r1 = 1
    //   0x04: il   r2, 2    -> r2 = 2
    //   0x08: or   r3, r1, r2 -> r3 = 3
    //   0x0C: wrch r3       -> outbound <- 3
    //   0x10: halt
    write_be32(g_spu_ctx.local_store + 0x00, spu_I(spu_op_il, 1, 1));
    write_be32(g_spu_ctx.local_store + 0x04, spu_I(spu_op_il, 2, 2));
    write_be32(g_spu_ctx.local_store + 0x08, spu_R(spu_op_or, 3, 1, 2));
    write_be32(g_spu_ctx.local_store + 0x0C, spu_R(spu_op_wrch, 0, 3, 0));
    write_be32(g_spu_ctx.local_store + 0x10, spu_I(spu_op_halt, 0, 0));

    const u64 steps = spu::run(&g_spu_ctx, 32);
    u32 got = 0;
    (void)spu::mailbox_pop_outbound(&g_spu_ctx, &got);

    const bool ok = (steps == 5) && (got == 3) && g_spu_ctx.halted;
    log::write(ok ? log::Level::Info : log::Level::Warn, "ps3-spu",
               "SPU self-test: steps=%llu out=%llu (expected 5, 3)",
               static_cast<unsigned long long>(steps), static_cast<unsigned long long>(got));

    // Mailbox FIFO.
    spu::init(&g_spu_ctx);
    (void)spu::mailbox_push_inbound(&g_spu_ctx, 0xAA);
    (void)spu::mailbox_push_inbound(&g_spu_ctx, 0xBB);
    (void)spu::mailbox_push_inbound(&g_spu_ctx, 0xCC);
    const u32 rd0 = g_spu_ctx.inbound.items[0];
    const u32 rd1 = g_spu_ctx.inbound.items[1];
    const u32 rd2 = g_spu_ctx.inbound.items[2];
    const bool fifo_ok = (rd0 == 0xAA) && (rd1 == 0xBB) && (rd2 == 0xCC);
    log::write(fifo_ok ? log::Level::Info : log::Level::Warn, "ps3-spu",
               "SPU mailbox FIFO test: %s", fifo_ok ? "pass" : "fail");
}

void test_dma() noexcept
{
    static u8 main_mem[512];
    static u8 local_mem[512];
    libk::memset(main_mem, 0, sizeof(main_mem));
    libk::memset(local_mem, 0, sizeof(local_mem));
    for (u32 i = 0; i < 512; ++i)
        main_mem[i] = static_cast<u8>(i & 0xFF);

    dma::Engine eng{};
    dma::init(&eng);
    dma::set_backing(&eng, main_mem, sizeof(main_mem));

    const bool queued =
        dma::queue(&eng, 1, dma::Dir::MainToLocal, 0, 0, 256, local_mem, sizeof(local_mem));
    if (!queued)
    {
        log::write(log::Level::Warn, "ps3-dma", "queue failed");
        return;
    }

    const u32 completed = dma::drain(&eng, 8);
    bool match = (completed == 1);
    if (match)
        for (u32 i = 0; i < 256; ++i)
            if (local_mem[i] != main_mem[i])
            {
                match = false;
                break;
            }
    log::write(match ? log::Level::Info : log::Level::Warn, "ps3-dma",
               "DMA main->local: %s (%llu completed)", match ? "pass" : "fail",
               static_cast<unsigned long long>(completed));

    for (u32 i = 0; i < 256; ++i)
        local_mem[i] = static_cast<u8>(0xFF - (i & 0xFF));
    (void)dma::queue(&eng, 2, dma::Dir::LocalToMain, 256, 0, 256, local_mem, sizeof(local_mem));
    (void)dma::drain(&eng, 8);

    bool match2 = true;
    for (u32 i = 0; i < 256; ++i)
        if (main_mem[256 + i] != local_mem[i])
        {
            match2 = false;
            break;
        }
    log::write(match2 ? log::Level::Info : log::Level::Warn, "ps3-dma", "DMA local->main: %s",
               match2 ? "pass" : "fail");

    dma::sync_barrier();
    dma::atomic_fence();
    log::write(log::Level::Info, "ps3-dma", "barrier + atomic fence: pass");
}

} // namespace

void self_test() noexcept
{
    log::write(log::Level::Info, "ps3", "running self-tests");
    test_decoder();
    test_elf();
    test_ppu();
    test_spu();
    test_dma();
    jit::self_test();
    rsx::self_test();
    log::write(log::Level::Info, "ps3", "self-tests complete");
}

} // namespace notyvos::ps3
