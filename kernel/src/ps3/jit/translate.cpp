#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/exec_page.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/ps3/jit/emitter.hpp>
#include <kernel/ps3/jit/translate.hpp>
#include <kernel/ps3/powerpc/decode.hpp>

#include <stddef.h>

namespace notyvos::ps3::jit
{

namespace
{
using powerpc::Instruction;

// --- Context offsets -------------------------------------------------------
// ppu::Context layout (see kernel/include/kernel/ps3/ppu.hpp):
//   u64 gpr[32]    offset   0
//   u64 fpr[32]    offset 256
//   u64 pc         offset 512
//   u64 lr         offset 520
//   u64 ctr        offset 528
//   u64 xer        offset 536
//   u32 cr         offset 544
//   ... function pointers after that ...
//
// The static_asserts below lock these in. If ppu::Context ever changes,
// this translation unit refuses to compile - which is what we want.

constexpr usize kOffGpr = offsetof(ppu::Context, gpr);
constexpr usize kOffPc = offsetof(ppu::Context, pc);
constexpr usize kOffLr = offsetof(ppu::Context, lr);
constexpr usize kOffCtr = offsetof(ppu::Context, ctr);
constexpr usize kOffXer = offsetof(ppu::Context, xer);

constexpr usize kGprBytes = static_cast<usize>(ppu::kGprCount) * 8u;
constexpr usize kFprBytes = static_cast<usize>(ppu::kFprCount) * 8u;

static_assert(kOffGpr == 0, "Context::gpr must be first");
static_assert(kOffPc == kOffGpr + kGprBytes + kFprBytes, "Context layout changed: pc");
static_assert(kOffLr == kOffPc + 8, "Context layout changed: lr");
static_assert(kOffCtr == kOffLr + 8, "Context layout changed: ctr");
static_assert(kOffXer == kOffCtr + 8, "Context layout changed: xer");

// GPR scratch registers used across emit sequences. Must be caller-saved
// so the JIT'd code does not need to preserve them.
constexpr u8 kScratch = reg::kRcx;
constexpr u8 kScratch2 = reg::kRdx;
constexpr u8 kCtx = reg::kR15;

constexpr usize kScratchBuf = 8192;

inline i32 gpr_disp(u32 n) noexcept
{
    return static_cast<i32>(kOffGpr + static_cast<usize>(n) * 8u);
}

// --- Emit helpers ----------------------------------------------------------

void emit_load_gpr(Emitter& e, u8 dst_x86, u32 src_ppc) noexcept
{
    if (src_ppc == 0)
    {
        e.zero_r(dst_x86);
        return;
    }
    e.mov_rm(dst_x86, kCtx, gpr_disp(src_ppc));
}

void emit_bump_pc(Emitter& e, u32 delta) noexcept
{
    e.alu_mem_imm8(alu::kAdd, kCtx, static_cast<i32>(kOffPc), static_cast<u8>(delta & 0xFFu));
}

void emit_set_pc_imm(Emitter& e, u64 value) noexcept
{
    e.mov_ri64(reg::kRax, value);
    e.mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
}

// --- D-form immediate arithmetic ------------------------------------------

// addi / addis / ori / oris / xori / xoris
bool emit_d_form_imm(Emitter& e, const Instruction& ins) noexcept
{
    const bool is_addi = (ins.opcode == 14);
    const bool is_addis = (ins.opcode == 15);
    const bool is_ori = (ins.opcode == 24);
    const bool is_oris = (ins.opcode == 25);
    const bool is_xori = (ins.opcode == 26);
    const bool is_xoris = (ins.opcode == 27);

    if (!(is_addi || is_addis || is_ori || is_oris || is_xori || is_xoris))
        return false;

    emit_load_gpr(e, kScratch, ins.ra);

    if (is_addi)
    {
        e.alu_ri32(alu::kAdd, kScratch, ins.simm);
    }
    else if (is_addis)
    {
        const i32 shifted = static_cast<i32>(static_cast<u32>(ins.simm) << 16);
        e.alu_ri32(alu::kAdd, kScratch, shifted);
    }
    else if (is_ori)
    {
        e.alu_ri32(alu::kOr, kScratch, static_cast<i32>(static_cast<u32>(ins.uimm)));
    }
    else if (is_oris)
    {
        e.alu_ri32(alu::kOr, kScratch, static_cast<i32>(static_cast<u32>(ins.uimm) << 16));
    }
    else if (is_xori)
    {
        e.alu_ri32(alu::kXor, kScratch, static_cast<i32>(static_cast<u32>(ins.uimm)));
    }
    else // is_xoris
    {
        e.alu_ri32(alu::kXor, kScratch, static_cast<i32>(static_cast<u32>(ins.uimm) << 16));
    }

    e.mov_mr(kCtx, gpr_disp(ins.rt), kScratch);
    emit_bump_pc(e, 4u);
    return true;
}

// --- X-form register-register ---------------------------------------------

// add / subf / and / or / xor
bool emit_x_form_rr(Emitter& e, const Instruction& ins) noexcept
{
    u8 op = 0;
    bool ok = true;

    switch (ins.xo)
    {
    case 266:
        op = alu::kAdd;
        break; // add
    case 40:
        op = alu::kSub;
        break; // subf  (rb - ra)
    case 28:
        op = alu::kAnd;
        break; // and
    case 444:
        op = alu::kOr;
        break; // or
    case 316:
        op = alu::kXor;
        break; // xor
    default:
        ok = false;
        break;
    }

    if (!ok)
        return false;

    if (ins.xo == 40)
    {
        // subf: GPR[rt] = GPR[rb] - GPR[ra]
        emit_load_gpr(e, kScratch2, ins.rb);
        emit_load_gpr(e, kScratch, ins.ra);
        e.alu_rr(alu::kSub, kScratch2, kScratch);
        e.mov_mr(kCtx, gpr_disp(ins.rt), kScratch2);
    }
    else
    {
        emit_load_gpr(e, kScratch, ins.rt);
        emit_load_gpr(e, kScratch2, ins.rb);
        e.alu_rr(op, kScratch, kScratch2);
        e.mov_mr(kCtx, gpr_disp(ins.rt), kScratch);
    }

    emit_bump_pc(e, 4u);
    return true;
}

// --- SPR access ------------------------------------------------------------

// mfspr rT, SPR   (xo == 339)
// mtspr SPR, rS   (xo == 467)
bool emit_spr(Emitter& e, const Instruction& ins) noexcept
{
    i32 target_disp = -1;
    if (ins.spr == 8)
        target_disp = static_cast<i32>(kOffLr);
    else if (ins.spr == 9)
        target_disp = static_cast<i32>(kOffCtr);
    else if (ins.spr == 1)
        target_disp = static_cast<i32>(kOffXer);
    else
        return false;

    if (ins.xo == 339)
    {
        e.mov_rm(kScratch, kCtx, target_disp);
        e.mov_mr(kCtx, gpr_disp(ins.rt), kScratch);
    }
    else if (ins.xo == 467)
    {
        emit_load_gpr(e, kScratch, ins.rt);
        e.mov_mr(kCtx, target_disp, kScratch);
    }
    else
    {
        return false;
    }

    emit_bump_pc(e, 4u);
    return true;
}

// --- Branch ---------------------------------------------------------------

// b / bl  (opcode 18).
bool emit_branch_i(Emitter& e, const Instruction& ins, u64 ins_pc) noexcept
{
    const u64 target = ins_pc + static_cast<u64>(ins.bdisp);

    if (ins.lk)
    {
        e.mov_ri64(reg::kRax, ins_pc + 4u);
        e.mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
    }
    emit_set_pc_imm(e, target);
    return true;
}

// bclr (xo == 16, unconditional when bo == 20).
bool emit_bclr(Emitter& e, const Instruction& ins, u64 ins_pc) noexcept
{
    if (ins.xo != 16)
        return false;

    const u32 bo = (ins.word >> 21) & 0x1Fu;
    if (bo != 20u)
        return false;

    if (ins.lk)
    {
        e.mov_ri64(reg::kRax, ins_pc + 4u);
        e.mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
    }
    e.mov_rm(reg::kRax, kCtx, static_cast<i32>(kOffLr));
    e.alu_ri32(alu::kAnd, reg::kRax, -4);
    e.mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
    return true;
}

// sc (opcode 17). The driver intercepts BlockEnd::Syscall and runs the
// instruction through ppu::step(), so the JIT only bumps PC here.
bool emit_sc(Emitter& e, u64 ins_pc) noexcept
{
    emit_set_pc_imm(e, ins_pc + 4u);
    return true;
}

// --- Dispatch -------------------------------------------------------------

bool emit_one(Emitter& e, const Instruction& ins, u64 ins_pc, BlockEnd& end) noexcept
{
    const u8 op = ins.opcode;

    if (op == 14 || op == 15 || (op >= 24 && op <= 27))
    {
        if (!emit_d_form_imm(e, ins))
            return false;
        return true;
    }

    if (op == 31)
    {
        if (ins.xo == 339 || ins.xo == 467)
        {
            if (!emit_spr(e, ins))
                return false;
            return true;
        }
        if (ins.xo == 266 || ins.xo == 40 || ins.xo == 28 || ins.xo == 444 || ins.xo == 316)
        {
            if (!emit_x_form_rr(e, ins))
                return false;
            return true;
        }
        return false;
    }

    if (op == 18)
    {
        if (!emit_branch_i(e, ins, ins_pc))
            return false;
        end = BlockEnd::Branch;
        return true;
    }

    if (op == 19)
    {
        if (!emit_bclr(e, ins, ins_pc))
            return false;
        end = BlockEnd::Branch;
        return true;
    }

    if (op == 17)
    {
        if (!emit_sc(e, ins_pc))
            return false;
        end = BlockEnd::Syscall;
        return true;
    }

    return false;
}

} // namespace

Block* translate_block(ppu::Context* ctx, u64 ppc_pc) noexcept
{
    if (!ctx || !ctx->read32)
        return nullptr;

    alignas(16) u8 scratch[kScratchBuf];
    Emitter e(scratch, sizeof(scratch));

    u64 pc = ppc_pc;
    BlockEnd end = BlockEnd::FallThrough;
    u32 count = 0;

    while (count < kMaxInsnsPerBlock)
    {
        u32 word = 0;
        if (!ctx->read32(ctx->user, pc, &word))
            break;

        const Instruction ins = powerpc::decode(word);
        BlockEnd local_end = BlockEnd::FallThrough;

        if (!emit_one(e, ins, pc, local_end))
        {
            // First instruction unsupported: refuse the block. Otherwise
            // the previous instruction is the block's last; stop cleanly.
            if (count == 0)
                return nullptr;
            end = BlockEnd::Unsupported;
            break;
        }

        pc += 4;
        ++count;

        if (local_end != BlockEnd::FallThrough)
        {
            end = local_end;
            break;
        }
    }

    if (count == 0)
        return nullptr;

    if (e.overflowed())
    {
        log::write(log::Level::Warn, "jit", "emitter overflow at ppc 0x%llx",
                   static_cast<unsigned long long>(ppc_pc));
        return nullptr;
    }

    e.ret();

    void* code = mm::ExecArena::publish(scratch, e.size());
    if (!code)
        return nullptr;

    auto* blk = static_cast<Block*>(mm::Heap::allocate(sizeof(Block)));
    if (!blk)
        return nullptr;
    libk::memset(blk, 0, sizeof(Block));

    blk->ppc_start = ppc_pc;
    blk->ppc_end = pc;
    blk->x86_code = static_cast<u8*>(code);
    blk->x86_size = static_cast<u32>(e.size());
    blk->insns = count;
    blk->end = end;
    blk->next = nullptr;

    return blk;
}

} // namespace notyvos::ps3::jit
