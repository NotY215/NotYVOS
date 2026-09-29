#pragma once
#include <kernel/ps3/jit/block.hpp>
#include <kernel/ps3/ppu.hpp>

namespace notyvos::ps3::jit
{

// Translate a straight-line block of PPC integer instructions starting at
// `ppc_pc`. Reads via ctx->read32. Returns nullptr if the *first*
// instruction cannot be translated (unsupported opcode or read failure).
//
// On success, the returned Block has been published to ExecArena and its
// x86-64 code written; the caller is responsible for inserting it into the
// TranslationCache.
Block* translate_block(ppu::Context* ctx, u64 ppc_pc) noexcept;

} // namespace notyvos::ps3::jit
