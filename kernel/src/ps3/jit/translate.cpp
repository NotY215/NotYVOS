#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/exec_page.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/ps3/jit/cache.hpp>
#include <kernel/ps3/jit/emitter.hpp>
#include <kernel/ps3/jit/translate.hpp>
#include <kernel/ps3/powerpc/decode.hpp>

#include <stddef.h>

namespace notyvos::ps3::jit
{

namespace
{
using powerpc::Instruction;

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

constexpr u8 kScratch = reg::kRcx;
constexpr u8 kScratch2 = reg::kRdx;
constexpr u8 kCtx = reg::kR15;

constexpr usize kScratchBuf = 32768;
constexpr u32 kMaxFailPatches = 64;
constexpr u64 kJitFault = 1;

inline i32 gpr_disp(u32 n) noexcept
{
    return static_cast<i32>(kOffGpr + static_cast<usize>(n) * 8u);
}

// DS-form displacement (14 bits, low two ignored).
inline i32 ds_disp(const Instruction& ins) noexcept
{
    return ins.simm & ~3;
}

// Build the 32-bit mask for MB..ME (inclusive), handling MB > ME wrap.
inline u32 build_mask(u32 mb, u32 me) noexcept
{
    const u32 m1 = (me == 31u) ? 0xFFFFFFFFu : ((1u << (me + 1u)) - 1u);
    const u32 m2 = (mb == 0u) ? 0u : ((1u << mb) - 1u);
    return (mb <= me) ? (m1 & ~m2) : (m1 | ~m2);
}
} // namespace

// ---------------------------------------------------------------------------
// extern "C" runtime helpers. Address is taken by the JIT and injected into
// the emitted code as a 64-bit immediate.
// ---------------------------------------------------------------------------
extern "C"
{

    // Forward decl so the store helpers can call it.
    void notyvos_jit_smc_check(u64 ea, u64 size) noexcept;

    bool notyvos_jit_load8(ppu::Context* ctx, u64 ea, u32 rt_disp) noexcept
    {
        if (!ctx->read8)
            return false;
        u8 v = 0;
        if (!ctx->read8(ctx->user, ea, &v))
            return false;
        *reinterpret_cast<u64*>(reinterpret_cast<u8*>(ctx) + rt_disp) = static_cast<u64>(v);
        return true;
    }

    bool notyvos_jit_load16(ppu::Context* ctx, u64 ea, u32 rt_disp) noexcept
    {
        if (!ctx->read16)
            return false;
        u16 v = 0;
        if (!ctx->read16(ctx->user, ea, &v))
            return false;
        *reinterpret_cast<u64*>(reinterpret_cast<u8*>(ctx) + rt_disp) = static_cast<u64>(v);
        return true;
    }

    bool notyvos_jit_load16s(ppu::Context* ctx, u64 ea, u32 rt_disp) noexcept
    {
        if (!ctx->read16)
            return false;
        u16 v = 0;
        if (!ctx->read16(ctx->user, ea, &v))
            return false;
        const i16 s = static_cast<i16>(v);
        *reinterpret_cast<u64*>(reinterpret_cast<u8*>(ctx) + rt_disp) =
            static_cast<u64>(static_cast<i64>(s));
        return true;
    }

    bool notyvos_jit_load32(ppu::Context* ctx, u64 ea, u32 rt_disp) noexcept
    {
        if (!ctx->read32)
            return false;
        u32 v = 0;
        if (!ctx->read32(ctx->user, ea, &v))
            return false;
        *reinterpret_cast<u64*>(reinterpret_cast<u8*>(ctx) + rt_disp) = static_cast<u64>(v);
        return true;
    }

    bool notyvos_jit_load32s(ppu::Context* ctx, u64 ea, u32 rt_disp) noexcept
    {
        if (!ctx->read32)
            return false;
        u32 v = 0;
        if (!ctx->read32(ctx->user, ea, &v))
            return false;
        const i32 s = static_cast<i32>(v);
        *reinterpret_cast<u64*>(reinterpret_cast<u8*>(ctx) + rt_disp) =
            static_cast<u64>(static_cast<i64>(s));
        return true;
    }

    bool notyvos_jit_load64(ppu::Context* ctx, u64 ea, u32 rt_disp) noexcept
    {
        if (!ctx->read64)
            return false;
        u64 v = 0;
        if (!ctx->read64(ctx->user, ea, &v))
            return false;
        *reinterpret_cast<u64*>(reinterpret_cast<u8*>(ctx) + rt_disp) = v;
        return true;
    }

    bool notyvos_jit_store8(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write8)
            return false;
        notyvos_jit_smc_check(ea, 1);
        return ctx->write8(ctx->user, ea, static_cast<u8>(value & 0xFFu));
    }

    bool notyvos_jit_store16(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write16)
            return false;
        notyvos_jit_smc_check(ea, 2);
        return ctx->write16(ctx->user, ea, static_cast<u16>(value & 0xFFFFu));
    }

    bool notyvos_jit_store32(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write32)
            return false;
        notyvos_jit_smc_check(ea, 4);
        return ctx->write32(ctx->user, ea, static_cast<u32>(value & 0xFFFFFFFFu));
    }

    bool notyvos_jit_store64(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write64)
            return false;
        notyvos_jit_smc_check(ea, 8);
        return ctx->write64(ctx->user, ea, value);
    }

    bool notyvos_jit_bc_test(ppu::Context* ctx, u32 bo, u32 bi) noexcept
    {
        bool ctr_ok = true;
        bool cr_ok = true;
        if ((bo & 0x04u) == 0)
            ctx->ctr = ctx->ctr - 1u;
        if ((bo & 0x01u) == 0)
        {
            const bool ctr_zero = (ctx->ctr == 0);
            ctr_ok = (ctr_zero == ((bo & 0x02u) != 0));
        }
        if ((bo & 0x10u) == 0)
        {
            const u32 bit_val = (ctx->cr >> (31u - bi)) & 1u;
            const bool cr_true = (bit_val != 0);
            cr_ok = (cr_true == ((bo & 0x08u) != 0));
        }
        return ctr_ok && cr_ok;
    }

    void notyvos_jit_smc_check(u64 ea, u64 size) noexcept
    {
        if (size == 0)
            return;
        const u64 start = ea & ~static_cast<u64>(3u);
        const u64 end = (ea + size + 3u) & ~static_cast<u64>(3u);
        for (u64 a = start; a < end; a += 4)
        {
            if (TranslationCache::probe(a))
                TranslationCache::invalidate_range(a, a + 4);
        }
    }

} // extern "C"

// ---------------------------------------------------------------------------
// Translator.
// ---------------------------------------------------------------------------
namespace
{

struct Translator
{
    Emitter* e;
    u64 block_start_pc;
    u32 fail_patch_off[kMaxFailPatches];
    u64 fail_patch_pc[kMaxFailPatches];
    u32 fail_count;

    void emit_call_checked(u64 helper_addr, u64 insn_pc) noexcept
    {
        e->mov_ri64(reg::kRax, helper_addr);
        e->call_r(reg::kRax);
        e->emit_u8(0x84);
        e->emit_u8(0xC0); // test al, al
        const u32 off = e->jcc_placeholder(cc::kE);
        if (fail_count < kMaxFailPatches)
        {
            fail_patch_off[fail_count] = off;
            fail_patch_pc[fail_count] = insn_pc;
        }
        ++fail_count;
    }

    void set_pc(u64 pc) noexcept
    {
        e->mov_ri64(reg::kRax, pc);
        e->mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
    }

    void load_gpr(u8 dst_x86, u32 src_ppc) noexcept
    {
        if (src_ppc == 0)
        {
            e->zero_r(dst_x86);
            return;
        }
        e->mov_rm(dst_x86, kCtx, gpr_disp(src_ppc));
    }

    void store_gpr(u32 dst_ppc, u8 src_x86) noexcept
    {
        e->mov_mr(kCtx, gpr_disp(dst_ppc), src_x86);
    }

    void emit_ea_into_rsi(u32 ra, i32 simm) noexcept
    {
        if (ra == 0)
        {
            e->mov_ri64(reg::kRsi, static_cast<u64>(static_cast<i64>(simm)));
        }
        else
        {
            e->mov_rm(reg::kRsi, kCtx, gpr_disp(ra));
            if (simm != 0)
                e->alu_ri32(alu::kAdd, reg::kRsi, simm);
        }
    }

    void emit_ea_indexed_into_rsi(u32 ra, u32 rb) noexcept
    {
        if (ra == 0)
        {
            load_gpr(reg::kRsi, rb);
        }
        else
        {
            e->mov_rm(reg::kRsi, kCtx, gpr_disp(ra));
            load_gpr(kScratch, rb);
            e->alu_rr(alu::kAdd, reg::kRsi, kScratch);
        }
    }

    // ----------------------------------------------------------------------
    // D-form immediate arithmetic
    // ----------------------------------------------------------------------
    bool d_form_imm(const Instruction& ins) noexcept
    {
        const u8 op = ins.opcode;
        const bool is_addi = (op == 14);
        const bool is_addis = (op == 15);
        const bool is_ori = (op == 24);
        const bool is_oris = (op == 25);
        const bool is_xori = (op == 26);
        const bool is_xoris = (op == 27);
        if (!(is_addi || is_addis || is_ori || is_oris || is_xori || is_xoris))
            return false;

        load_gpr(kScratch, ins.ra);
        if (is_addi)
            e->alu_ri32(alu::kAdd, kScratch, ins.simm);
        else if (is_addis)
            e->alu_ri32(alu::kAdd, kScratch, static_cast<i32>(static_cast<u32>(ins.simm) << 16));
        else if (is_ori)
            e->alu_ri32(alu::kOr, kScratch, static_cast<i32>(ins.uimm));
        else if (is_oris)
            e->alu_ri32(alu::kOr, kScratch, static_cast<i32>(ins.uimm << 16));
        else if (is_xori)
            e->alu_ri32(alu::kXor, kScratch, static_cast<i32>(ins.uimm));
        else
            e->alu_ri32(alu::kXor, kScratch, static_cast<i32>(ins.uimm << 16));
        store_gpr(ins.rt, kScratch);
        return true;
    }

    // ----------------------------------------------------------------------
    // X-form register-register arithmetic
    // ----------------------------------------------------------------------
    bool x_form_rr(const Instruction& ins) noexcept
    {
        u8 op = 0;
        bool is_subf = false;
        switch (ins.xo)
        {
        case 266:
            op = alu::kAdd;
            break; // add
        case 40:
            op = alu::kSub;
            is_subf = true;
            break; // subf (rb - ra)
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
            return false;
        }

        if (is_subf)
        {
            load_gpr(kScratch2, ins.rb);
            load_gpr(kScratch, ins.ra);
            e->alu_rr(alu::kSub, kScratch2, kScratch);
            store_gpr(ins.rt, kScratch2);
        }
        else
        {
            load_gpr(kScratch, ins.ra);
            load_gpr(kScratch2, ins.rb);
            e->alu_rr(op, kScratch, kScratch2);
            store_gpr(ins.rt, kScratch);
        }
        return true;
    }

    // ----------------------------------------------------------------------
    // mfspr / mtspr for LR / CTR / XER
    // ----------------------------------------------------------------------
    bool spr_access(const Instruction& ins) noexcept
    {
        i32 disp = -1;
        if (ins.spr == 8)
            disp = static_cast<i32>(kOffLr);
        else if (ins.spr == 9)
            disp = static_cast<i32>(kOffCtr);
        else if (ins.spr == 1)
            disp = static_cast<i32>(kOffXer);
        else
            return false;

        if (ins.xo == 339) // mfspr
        {
            e->mov_rm(kScratch, kCtx, disp);
            store_gpr(ins.rt, kScratch);
        }
        else if (ins.xo == 467) // mtspr
        {
            load_gpr(kScratch, ins.rt);
            e->mov_mr(kCtx, disp, kScratch);
        }
        else
            return false;
        return true;
    }

    // ----------------------------------------------------------------------
    // extsb / extsh
    // ----------------------------------------------------------------------
    bool extsb(const Instruction& ins) noexcept
    {
        load_gpr(kScratch, ins.rt);
        e->shl_ri8_64(kScratch, 56);
        e->sar_ri8_64(kScratch, 56);
        store_gpr(ins.ra, kScratch);
        return true;
    }

    bool extsh(const Instruction& ins) noexcept
    {
        load_gpr(kScratch, ins.rt);
        e->shl_ri8_64(kScratch, 48);
        e->sar_ri8_64(kScratch, 48);
        store_gpr(ins.ra, kScratch);
        return true;
    }

    // ----------------------------------------------------------------------
    // srawi (immediate arithmetic shift right)
    // ----------------------------------------------------------------------
    bool srawi(const Instruction& ins) noexcept
    {
        const u32 sh = (ins.word >> 11) & 0x1Fu;
        e->mov_r32_mem(kScratch, kCtx, gpr_disp(ins.rt)); // 32-bit load, zero-extends
        e->sar_ri8_32(kScratch, static_cast<u8>(sh));     // 32-bit SAR, zero-extends
        store_gpr(ins.ra, kScratch);
        return true;
    }

    // ----------------------------------------------------------------------
    // rlwinm / rlwimi / rlwnm
    // ----------------------------------------------------------------------
    bool rlwinm(const Instruction& ins) noexcept
    {
        const u32 rs = (ins.word >> 21) & 0x1Fu;
        const u32 ra = (ins.word >> 16) & 0x1Fu;
        const u32 sh = (ins.word >> 11) & 0x1Fu;
        const u32 mb = (ins.word >> 6) & 0x1Fu;
        const u32 me = (ins.word >> 1) & 0x1Fu;
        const u32 mask = build_mask(mb, me);

        e->mov_r32_mem(kScratch, kCtx, gpr_disp(rs));
        if (sh != 0)
            e->rol_ri8_32(kScratch, static_cast<u8>(sh));
        e->alu_ri32(alu::kAnd, kScratch, static_cast<i32>(mask));
        store_gpr(ra, kScratch);
        return true;
    }

    bool rlwimi(const Instruction& ins) noexcept
    {
        const u32 rs = (ins.word >> 21) & 0x1Fu;
        const u32 ra = (ins.word >> 16) & 0x1Fu;
        const u32 sh = (ins.word >> 11) & 0x1Fu;
        const u32 mb = (ins.word >> 6) & 0x1Fu;
        const u32 me = (ins.word >> 1) & 0x1Fu;
        const u32 mask = build_mask(mb, me);
        const u32 nmask = ~mask;

        // scratch  = rotl32(GPR[rs], sh) & mask
        e->mov_r32_mem(kScratch, kCtx, gpr_disp(rs));
        if (sh != 0)
            e->rol_ri8_32(kScratch, static_cast<u8>(sh));
        e->alu_ri32(alu::kAnd, kScratch, static_cast<i32>(mask));

        // scratch2 = GPR[ra] & ~mask
        e->mov_r32_mem(kScratch2, kCtx, gpr_disp(ra));
        e->alu_ri32(alu::kAnd, kScratch2, static_cast<i32>(nmask));

        // result = scratch | scratch2
        e->alu_rr(alu::kOr, kScratch, kScratch2);
        store_gpr(ra, kScratch);
        return true;
    }

    bool rlwnm(const Instruction& ins) noexcept
    {
        const u32 rs = (ins.word >> 21) & 0x1Fu;
        const u32 ra = (ins.word >> 16) & 0x1Fu;
        const u32 rb = (ins.word >> 11) & 0x1Fu; // shift amount register
        const u32 mb = (ins.word >> 6) & 0x1Fu;
        const u32 me = (ins.word >> 1) & 0x1Fu;
        const u32 mask = build_mask(mb, me);

        // rcx = GPR[rb] & 31   (CL is the shift count)
        load_gpr(kScratch, rb);
        e->alu_ri32(alu::kAnd, kScratch, 0x1F);

        // edx = low32(GPR[rs]); rol edx, cl; and edx, mask
        e->mov_r32_mem(kScratch2, kCtx, gpr_disp(rs));
        e->rol_r32_cl(kScratch2);
        e->alu_ri32(alu::kAnd, kScratch2, static_cast<i32>(mask));
        store_gpr(ra, kScratch2);
        return true;
    }

    // ----------------------------------------------------------------------
    // D-form loads / stores (byte-offset immediate, sign-extended)
    // ----------------------------------------------------------------------
    bool emit_load_d(const Instruction& ins, u64 ins_pc, u64 helper) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ins.simm);
        e->mov_ri32(reg::kRdx, static_cast<u32>(gpr_disp(ins.rt)));
        emit_call_checked(helper, ins_pc);
        return true;
    }

    bool emit_store_d(const Instruction& ins, u64 ins_pc, u64 helper) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ins.simm);
        if (ins.rt == 0)
            e->zero_r(reg::kRdx);
        else
            e->mov_rm(reg::kRdx, kCtx, gpr_disp(ins.rt));
        emit_call_checked(helper, ins_pc);
        return true;
    }

    // DS-form (ld/std/lwa): 14-bit displacement, low two bits ignored
    bool emit_load_ds(const Instruction& ins, u64 ins_pc, u64 helper) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ds_disp(ins));
        e->mov_ri32(reg::kRdx, static_cast<u32>(gpr_disp(ins.rt)));
        emit_call_checked(helper, ins_pc);
        return true;
    }

    bool emit_store_ds(const Instruction& ins, u64 ins_pc, u64 helper) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ds_disp(ins));
        if (ins.rt == 0)
            e->zero_r(reg::kRdx);
        else
            e->mov_rm(reg::kRdx, kCtx, gpr_disp(ins.rt));
        emit_call_checked(helper, ins_pc);
        return true;
    }

    // X-form indexed loads / stores (EA = (RA==0?0:GPR[RA]) + GPR[RB])
    bool emit_load_x(const Instruction& ins, u64 ins_pc, u64 helper) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_indexed_into_rsi(ins.ra, ins.rb);
        e->mov_ri32(reg::kRdx, static_cast<u32>(gpr_disp(ins.rt)));
        emit_call_checked(helper, ins_pc);
        return true;
    }

    bool emit_store_x(const Instruction& ins, u64 ins_pc, u64 helper) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_indexed_into_rsi(ins.ra, ins.rb);
        if (ins.rt == 0)
            e->zero_r(reg::kRdx);
        else
            e->mov_rm(reg::kRdx, kCtx, gpr_disp(ins.rt));
        emit_call_checked(helper, ins_pc);
        return true;
    }

    // ----------------------------------------------------------------------
    // Branches
    // ----------------------------------------------------------------------
    bool branch_i(const Instruction& ins, u64 ins_pc) noexcept
    {
        const u64 target = ins_pc + static_cast<u64>(static_cast<i64>(ins.bdisp));
        if (ins.lk)
        {
            e->mov_ri64(reg::kRax, ins_pc + 4u);
            e->mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
        }
        set_pc(target);
        return true;
    }

    bool bclr_uncond(const Instruction& ins, u64 ins_pc) noexcept
    {
        if (ins.xo != 16)
            return false;
        const u32 bo = (ins.word >> 21) & 0x1Fu;
        if (bo != 20u)
            return false;
        if (ins.lk)
        {
            e->mov_ri64(reg::kRax, ins_pc + 4u);
            e->mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
        }
        e->mov_rm(reg::kRax, kCtx, static_cast<i32>(kOffLr));
        e->alu_ri32(alu::kAnd, reg::kRax, -4);
        e->mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
        return true;
    }

    bool bc_cond(const Instruction& ins, u64 ins_pc) noexcept
    {
        const u32 bo = (ins.word >> 21) & 0x1Fu;
        const u32 bi = (ins.word >> 16) & 0x1Fu;

        if (bo == 20u)
        {
            const u64 target = ins_pc + static_cast<u64>(static_cast<i64>(ins.bdisp));
            if (ins.lk)
            {
                e->mov_ri64(reg::kRax, ins_pc + 4u);
                e->mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
            }
            set_pc(target);
            return true;
        }

        const u64 helper = reinterpret_cast<u64>(&notyvos_jit_bc_test);
        e->mov_rr(reg::kRdi, kCtx);
        e->mov_ri32(reg::kRsi, bo);
        e->mov_ri32(reg::kRdx, bi);
        e->mov_ri64(reg::kRax, helper);
        e->call_r(reg::kRax);
        e->emit_u8(0x84);
        e->emit_u8(0xC0);

        const u32 jnz_taken = e->jcc_placeholder(cc::kNe);
        set_pc(ins_pc + 4u);
        const u32 jmp_end = e->jmp_placeholder();

        e->patch_branch_to_here(jnz_taken);
        const u64 target = ins_pc + static_cast<u64>(static_cast<i64>(ins.bdisp));
        if (ins.lk)
        {
            e->mov_ri64(reg::kRax, ins_pc + 4u);
            e->mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
        }
        set_pc(target);
        e->patch_branch_to_here(jmp_end);
        return true;
    }

    bool bcctr_cond(const Instruction& ins, u64 ins_pc) noexcept
    {
        if (ins.xo != 528)
            return false;
        const u32 bo = (ins.word >> 21) & 0x1Fu;
        const u32 bi = (ins.word >> 16) & 0x1Fu;

        if (bo == 20u)
        {
            if (ins.lk)
            {
                e->mov_ri64(reg::kRax, ins_pc + 4u);
                e->mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
            }
            e->mov_rm(reg::kRax, kCtx, static_cast<i32>(kOffCtr));
            e->alu_ri32(alu::kAnd, reg::kRax, -4);
            e->mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
            return true;
        }

        const u64 helper = reinterpret_cast<u64>(&notyvos_jit_bc_test);
        e->mov_rr(reg::kRdi, kCtx);
        e->mov_ri32(reg::kRsi, bo);
        e->mov_ri32(reg::kRdx, bi);
        e->mov_ri64(reg::kRax, helper);
        e->call_r(reg::kRax);
        e->emit_u8(0x84);
        e->emit_u8(0xC0);

        const u32 jnz_taken = e->jcc_placeholder(cc::kNe);
        set_pc(ins_pc + 4u);
        const u32 jmp_end = e->jmp_placeholder();

        e->patch_branch_to_here(jnz_taken);
        if (ins.lk)
        {
            e->mov_ri64(reg::kRax, ins_pc + 4u);
            e->mov_mr(kCtx, static_cast<i32>(kOffLr), reg::kRax);
        }
        e->mov_rm(reg::kRax, kCtx, static_cast<i32>(kOffCtr));
        e->alu_ri32(alu::kAnd, reg::kRax, -4);
        e->mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
        e->patch_branch_to_here(jmp_end);
        return true;
    }

    bool sc(const Instruction&, u64 ins_pc) noexcept
    {
        set_pc(ins_pc + 4u);
        return true;
    }

    // ----------------------------------------------------------------------
    // Per-instruction dispatch.
    // ----------------------------------------------------------------------
    bool one(const Instruction& ins, u64 ins_pc, BlockEnd& end) noexcept
    {
        const u8 op = ins.opcode;

        // D-form immediate arithmetic.
        if (op == 14 || op == 15 || (op >= 24 && op <= 27))
            return d_form_imm(ins);

        // rlwinm / rlwimi / rlwnm (their own primary opcodes).
        if (op == 20)
            return rlwimi(ins);
        if (op == 21)
            return rlwinm(ins);
        if (op == 23)
            return rlwnm(ins);

        // Primary 31: X/XO/XFX.
        if (op == 31)
        {
            // SPR access.
            if (ins.xo == 339 || ins.xo == 467)
                return spr_access(ins);

            // Register-register arithmetic.
            if (ins.xo == 266 || ins.xo == 40 || ins.xo == 28 || ins.xo == 444 || ins.xo == 316)
                return x_form_rr(ins);

            // Sign-extend.
            if (ins.xo == 954)
                return extsb(ins);
            if (ins.xo == 986)
                return extsh(ins);

            // Arithmetic shift right immediate.
            if (ins.xo == 824)
                return srawi(ins);

            // Indexed loads: lwzx 23, lbzx 87, lhzx 279, lhax 343, ldx 21.
            if (ins.xo == 23)
                return emit_load_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load32));
            if (ins.xo == 87)
                return emit_load_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load8));
            if (ins.xo == 279)
                return emit_load_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load16));
            if (ins.xo == 343)
                return emit_load_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load16s));
            if (ins.xo == 21)
                return emit_load_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load64));

            // Indexed stores: stwx 151, stbx 215, sthx 407, stdx 149.
            if (ins.xo == 151)
                return emit_store_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_store32));
            if (ins.xo == 215)
                return emit_store_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_store8));
            if (ins.xo == 407)
                return emit_store_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_store16));
            if (ins.xo == 149)
                return emit_store_x(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_store64));

            return false;
        }

        // DS-form 64-bit memory ops.
        if (op == 58) // ld / ldu / lwa
        {
            if (ins.xo == 0)
                return emit_load_ds(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load64));
            if (ins.xo == 2)
                return emit_load_ds(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_load32s));
            return false;
        }
        if (op == 62) // std / stdu
        {
            if (ins.xo == 0)
                return emit_store_ds(ins, ins_pc, reinterpret_cast<u64>(&notyvos_jit_store64));
            return false;
        }

        // D-form 32/16/8-bit memory ops.
        if (op == 32)
            return emit_load_d(ins, ins_pc,
                               reinterpret_cast<u64>(&notyvos_jit_load32)); // lwz
        if (op == 34)
            return emit_load_d(ins, ins_pc,
                               reinterpret_cast<u64>(&notyvos_jit_load8)); // lbz
        if (op == 40)
            return emit_load_d(ins, ins_pc,
                               reinterpret_cast<u64>(&notyvos_jit_load16)); // lhz
        if (op == 42)
            return emit_load_d(ins, ins_pc,
                               reinterpret_cast<u64>(&notyvos_jit_load16s)); // lha
        if (op == 36)
            return emit_store_d(ins, ins_pc,
                                reinterpret_cast<u64>(&notyvos_jit_store32)); // stw
        if (op == 38)
            return emit_store_d(ins, ins_pc,
                                reinterpret_cast<u64>(&notyvos_jit_store8)); // stb
        if (op == 44)
            return emit_store_d(ins, ins_pc,
                                reinterpret_cast<u64>(&notyvos_jit_store16)); // sth

        // Branch family.
        if (op == 18)
        {
            if (!branch_i(ins, ins_pc))
                return false;
            end = BlockEnd::Branch;
            return true;
        }
        if (op == 19)
        {
            if (ins.xo == 16)
            {
                if (!bclr_uncond(ins, ins_pc))
                    return false;
            }
            else if (ins.xo == 528)
            {
                if (!bcctr_cond(ins, ins_pc))
                    return false;
            }
            else
                return false;
            end = BlockEnd::Branch;
            return true;
        }
        if (op == 16)
        {
            if (!bc_cond(ins, ins_pc))
                return false;
            end = BlockEnd::Branch;
            return true;
        }
        if (op == 17)
        {
            if (!sc(ins, ins_pc))
                return false;
            end = BlockEnd::Syscall;
            return true;
        }

        return false;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Public entry point.
// ---------------------------------------------------------------------------
Block* translate_block(ppu::Context* ctx, u64 ppc_pc) noexcept
{
    if (!ctx || !ctx->read32)
        return nullptr;

    alignas(16) u8 scratch[kScratchBuf];
    Emitter e(scratch, sizeof(scratch));

    // Per-block prologue: align RSP so helper calls satisfy the System V ABI.
    // Each chained block does the same prologue, and each exit path does the
    // matching epilogue before returning.
    e.sub_rsp_imm8(8);

    Translator t{};
    t.e = &e;
    t.block_start_pc = ppc_pc;
    t.fail_count = 0;

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

        if (!t.one(ins, pc, local_end))
        {
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

    if (e.overflowed() || t.fail_count > kMaxFailPatches)
    {
        log::write(log::Level::Warn, "jit", "emitter overflow at ppc 0x%llx",
                   static_cast<unsigned long long>(ppc_pc));
        return nullptr;
    }

    if (end == BlockEnd::FallThrough)
        t.set_pc(pc);

    // Normal exit: status 0, restore RSP, RET.
    e.zero_r(reg::kRax);
    e.add_rsp_imm8(8);
    e.ret();

    // Fault exits: one per memory-op site. Each restores RSP, sets the
    // PPC pc back to the failing instruction, and returns status 1.
    for (u32 i = 0; i < t.fail_count; ++i)
    {
        e.patch_branch_to_here(t.fail_patch_off[i]);
        e.mov_ri64(reg::kRax, t.fail_patch_pc[i]);
        e.mov_mr(kCtx, static_cast<i32>(kOffPc), reg::kRax);
        e.mov_ri32(reg::kRax, static_cast<u32>(kJitFault));
        e.add_rsp_imm8(8);
        e.ret();
    }

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
