#include <kernel/log.hpp>
#include <kernel/ps3/jit/cache.hpp>
#include <kernel/ps3/jit/jit.hpp>
#include <kernel/ps3/jit/translate.hpp>

namespace notyvos::ps3::jit
{

// Assembly trampoline (jit_enter.S). Returns the RAX value the JIT'd code
// placed before RET. 0 = normal exit, 1 = memory fault mid-block.
extern "C" u64 notyvos_jit_enter(void* ctx, void* code) noexcept;

namespace
{
u64 g_blocks_translated = 0;
u64 g_blocks_entered = 0;
u64 g_fallback_steps = 0;
u64 g_faults = 0;
bool g_ready = false;

constexpr u64 kJitOk = 0;
constexpr u64 kJitFault = 1;
} // namespace

void init() noexcept
{
    TranslationCache::init();
    g_blocks_translated = 0;
    g_blocks_entered = 0;
    g_fallback_steps = 0;
    g_faults = 0;
    g_ready = true;
    log::write(log::Level::Info, "jit", "baseline JIT ready (5B)");
}

u64 run(ppu::Context* ctx, u64 max_steps) noexcept
{
    if (!g_ready || !ctx)
        return 0;

    u64 executed = 0;

    while (executed < max_steps)
    {
        Block* blk = TranslationCache::lookup(ctx->pc);
        if (!blk)
        {
            blk = translate_block(ctx, ctx->pc);
            if (!blk)
            {
                if (!ppu::step(ctx))
                    break;
                ++executed;
                ++g_fallback_steps;
                continue;
            }
            TranslationCache::insert(blk);
            ++g_blocks_translated;
        }

        if (blk->end == BlockEnd::Syscall)
        {
            // The whole `sc` sequence is executed via the interpreter so
            // the callback path stays identical to the pre-JIT baseline.
            if (!ppu::step(ctx))
                break;
            ++executed;
            ++g_fallback_steps;
            continue;
        }

        const u64 status = notyvos_jit_enter(ctx, blk->x86_code);
        ++g_blocks_entered;

        if (status == kJitFault)
        {
            // A memory op inside the block failed. Run the failing insn
            // via the interpreter. If the interpreter also fails, break.
            ++g_faults;
            if (!ppu::step(ctx))
                break;
            ++executed;
            ++g_fallback_steps;
            continue;
        }

        (void)kJitOk;
        executed += blk->insns;

        if (blk->end == BlockEnd::Unsupported)
        {
            if (!ppu::step(ctx))
                break;
            ++executed;
            ++g_fallback_steps;
        }
    }

    return executed;
}

u64 blocks_translated() noexcept
{
    return g_blocks_translated;
}
u64 blocks_entered() noexcept
{
    return g_blocks_entered;
}
u64 fallback_steps() noexcept
{
    return g_fallback_steps;
}
u64 fault_count() noexcept
{
    return g_faults;
}

} // namespace notyvos::ps3::jit
