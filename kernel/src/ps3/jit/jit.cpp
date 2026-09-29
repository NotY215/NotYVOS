#include <kernel/log.hpp>
#include <kernel/ps3/jit/cache.hpp>
#include <kernel/ps3/jit/jit.hpp>
#include <kernel/ps3/jit/translate.hpp>

namespace notyvos::ps3::jit
{

// Assembly trampoline (jit_enter.S).
extern "C" void notyvos_jit_enter(void* ctx, void* code) noexcept;

namespace
{
u64 g_blocks_translated = 0;
u64 g_blocks_entered = 0;
u64 g_fallback_steps = 0;
bool g_ready = false;
} // namespace

void init() noexcept
{
    TranslationCache::init();
    g_blocks_translated = 0;
    g_blocks_entered = 0;
    g_fallback_steps = 0;
    g_ready = true;
    log::write(log::Level::Info, "jit", "baseline JIT ready");
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
                // Translator cannot handle the first instruction at
                // ctx->pc. Delegate to the interpreter for one step.
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
            // Let the interpreter dispatch the syscall (it also performs
            // the pc += 4 step, matching the interpreter's own contract).
            // We do NOT enter the JIT'd code for sc blocks; we let the
            // interpreter execute the whole instruction to keep the
            // callback path identical to the pre-JIT baseline.
            //
            // The translated block for a Syscall terminator is currently
            // unused; re-translating the sequence via the interpreter costs
            // one extra decode per sc, but only once per sc site since it
            // isn't cached in that path.
            // Simpler: execute the *whole* block via the interpreter.
            if (!ppu::step(ctx))
                break;
            ++executed;
            ++g_fallback_steps;
            continue;
        }

        notyvos_jit_enter(ctx, blk->x86_code);
        ++g_blocks_entered;

        const u32 n = blk->insns;
        executed += n;

        if (blk->end == BlockEnd::Unsupported)
        {
            // Last translated insn is before the unsupported one. Run it
            // through the interpreter.
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

} // namespace notyvos::ps3::jit
