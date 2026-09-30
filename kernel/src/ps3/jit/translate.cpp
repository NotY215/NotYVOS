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

constexpr usize kScratchBuf = 16384;
constexpr u32 kMaxFailPatches = 32;

constexpr u64 kJitFault = 1;

inline i32 gpr_disp(u32 n) noexcept
{
    return static_cast<i32>(kOffGpr + static_cast<usize>(n) * 8u);
}

// DS-form displacement: 14 bits, lower two bits ignored.
inline i32 ds_disp(const Instruction& ins) noexcept
{
    return ins.simm & ~3;
}
} // namespace

// ---------------------------------------------------------------------------
// Runtime helpers.
// ---------------------------------------------------------------------------
extern "C"
{

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

    // lha: load halfword algebraic (sign-extended) into a 64-bit GPR.
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

    bool notyvos_jit_store8(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write8)
            return false;
        return ctx->write8(ctx->user, ea, static_cast<u8>(value & 0xFFu));
    }

    bool notyvos_jit_store16(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write16)
            return false;
        return ctx->write16(ctx->user, ea, static_cast<u16>(value & 0xFFFFu));
    }

    bool notyvos_jit_store32(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write32)
            return false;
        return ctx->write32(ctx->user, ea, static_cast<u32>(value & 0xFFFFFFFFu));
    }

    bool notyvos_jit_store64(ppu::Context* ctx, u64 ea, u64 value) noexcept
    {
        if (!ctx->write64)
            return false;
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

} // extern "C"

// ---------------------------------------------------------------------------
// Translator internals.
// ---------------------------------------------------------------------------
namespace
{

struct Translator
{
    Emitter* e;
    u32 fail_patch_off[kMaxFailPatches];
    u64 fail_patch_pc[kMaxFailPatches];
    u32 fail_count;

    void emit_call_checked(u64 helper_addr, u64 insn_pc) noexcept
    {
        e->mov_ri64(reg::kRax, helper_addr);
        e->call_r(reg::kRax);
        e->emit_u8(0x84);
        e->emit_u8(0xC0);
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

    // ---- Integer / logical -----------------------------------------------

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

    bool x_form_rr(const Instruction& ins) noexcept
    {
        u8 op = 0;
        bool is_subf = false;
        switch (ins.xo)
        {
        case 266:
            op = alu::kAdd;
            break;
        case 40:
            op = alu::kSub;
            is_subf = true;
            break;
        case 28:
            op = alu::kAnd;
            break;
        case 444:
            op = alu::kOr;
            break;
        case 316:
            op = alu::kXor;
            break;
        default:
            return false;
        }

        // PPC: X-form arithmetic/logical takes rS (source) in the rt slot for
        // AND/OR/XOR, and rD in the rt slot for ADD/SUBF. In all cases, the
        // operands are (rt, ra) or (ra, rb). We load rt and rb for ADD/AND/
        // OR/XOR, and ra and rb for SUBF.
        if (is_subf)
        {
            // subf rD, rA, rB  =>  rD = rB - rA
            load_gpr(kScratch2, ins.rb);
            load_gpr(kScratch, ins.ra);
            e->alu_rr(alu::kSub, kScratch2, kScratch);
            store_gpr(ins.rt, kScratch2);
        }
        else
        {
            // add/and/or/xor rD, rA, rB  =>  rD = rA OP rB
            load_gpr(kScratch, ins.ra);
            load_gpr(kScratch2, ins.rb);
            e->alu_rr(op, kScratch, kScratch2);
            store_gpr(ins.rt, kScratch);
        }
        return true;
    }

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

        if (ins.xo == 339)
        {
            e->mov_rm(kScratch, kCtx, disp);
            store_gpr(ins.rt, kScratch);
        }
        else if (ins.xo == 467)
        {
            load_gpr(kScratch, ins.rt);
            e->mov_mr(kCtx, disp, kScratch);
        }
        else
        {
            return false;
        }
        return true;
    }

    // ---- 5C: sign-extend byte / halfword ---------------------------------

    bool extsb(const Instruction& ins) noexcept
    {
        // extsb rA, rS  => rA = sign_extend_8(rS[7:0])
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

    // ---- 5C: srawi -------------------------------------------------------

    bool srawi(const Instruction& ins) noexcept
    {
        // srawi rA, rS, SH  =>  rA = sign_extend_32(rS[31:0] >> SH) as u64
        // SH is bits 11-15.
        const u32 sh = (ins.word >> 11) & 0x1Fu;
        // Load full 64 bits but operate on the low 32 by writing to a 32-bit
        // register first. mov r32, m32 zeroes the upper half.
        e->mov_r32_mem(kScratch, kCtx, gpr_disp(ins.rt));
        e->sar_ri8_32(kScratch, static_cast<u8>(sh));
        // Writing r32 to memory at the GPR slot stores zero-extended 64-bit;
        // PPC semantics require the 32-bit signed result zero-extended.
        store_gpr(ins.ra, kScratch);
        return true;
    }

    // ---- Memory ----------------------------------------------------------

    bool emit_load(const Instruction& ins, u64 ins_pc, u64 helper_addr) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ins.simm);
        e->mov_ri32(reg::kRdx, static_cast<u32>(gpr_disp(ins.rt)));
        emit_call_checked(helper_addr, ins_pc);
        return true;
    }

    bool emit_load_ds(const Instruction& ins, u64 ins_pc, u64 helper_addr) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ds_disp(ins));
        e->mov_ri32(reg::kRdx, static_cast<u32>(gpr_disp(ins.rt)));
        emit_call_checked(helper_addr, ins_pc);
        return true;
    }

    bool emit_store(const Instruction& ins, u64 ins_pc, u64 helper_addr) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ins.simm);
        if (ins.rt == 0)
            e->zero_r(reg::kRdx);
        else
            e->mov_rm(reg::kRdx, kCtx, gpr_disp(ins.rt));
        emit_call_checked(helper_addr, ins_pc);
        return true;
    }

    bool emit_store_ds(const Instruction& ins, u64 ins_pc, u64 helper_addr) noexcept
    {
        e->mov_rr(reg::kRdi, kCtx);
        emit_ea_into_rsi(ins.ra, ds_disp(ins));
        if (ins.rt == 0)
            e->zero_r(reg::kRdx);
        else
            e->mov_rm(reg::kRdx, kCtx, gpr_disp(ins.rt));
        emit_call_checked(helper_addr, ins_pc);
        return true;
    }

    // ---- Branches --------------------------------------------------------

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

    // ---------------------------------------------------------------------

    bool one(const Instruction& ins, u64 ins_pc, BlockEnd& end) noexcept
    {
        const u8 op = ins.opcode;

        if (op == 14 || op == 15 || (op >= 24 && op <= 27))
            return d_form_imm(ins);

        if (op == 31)
        {
            if (ins.xo == 339 || ins.xo == 467)
                return spr_access(ins);
            if (ins.xo == 954)
                return extsb(ins); // 5C
            if (ins.xo == 986)
                return extsh(ins); // 5C
            if (ins.xo == 824)
                return srawi(ins); // 5C
            if (ins.xo == 266 || ins.xo == 40 || ins.xo == 28 || ins.xo == 444 || ins.xo == 316)
                return x_form_rr(ins);
            return false;
        }

        // 5C 64-bit memory ops.
        if (op == 58)
        {
            if (ins.xo == 0)
                return emit_load_ds(ins, ins_pc,
                                    reinterpret_cast<u64>(&notyvos_jit_load64)); // ld
            if (ins.xo == 2)
                return emit_load_ds(ins, ins_pc,
                                    reinterpret_cast<u64>(&notyvos_jit_load32s)); // lwa
            return false;
        }
        if (op == 62)
        {
            if (ins.xo == 0)
                return emit_store_ds(ins, ins_pc,
                                     reinterpret_cast<u64>(&notyvos_jit_store64)); // std
            return false;
        }

        // 5B 32/16/8-bit memory ops.
        if (op == 32)
            return emit_load(ins, ins_pc,
                             reinterpret_cast<u64>(&notyvos_jit_load32)); // lwz
        if (op == 34)
            return emit_load(ins, ins_pc,
                             reinterpret_cast<u64>(&notyvos_jit_load8)); // lbz
        if (op == 40)
            return emit_load(ins, ins_pc,
                             reinterpret_cast<u64>(&notyvos_jit_load16)); // lhz
        if (op == 42)
            return emit_load(ins, ins_pc,
                             reinterpret_cast<u64>(&notyvos_jit_load16s)); // lha
        if (op == 36)
            return emit_store(ins, ins_pc,
                              reinterpret_cast<u64>(&notyvos_jit_store32)); // stw
        if (op == 38)
            return emit_store(ins, ins_pc,
                              reinterpret_cast<u64>(&notyvos_jit_store8)); // stb
        if (op == 44)
            return emit_store(ins, ins_pc,
                              reinterpret_cast<u64>(&notyvos_jit_store16)); // sth

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
            {
                return false;
            }
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

Block* translate_block(ppu::Context* ctx, u64 ppc_pc) noexcept
{
    if (!ctx || !ctx->read32)
        return nullptr;

    alignas(16) u8 scratch[kScratchBuf];
    Emitter e(scratch, sizeof(scratch));

    e.sub_rsp_imm8(8);

    Translator t{};
    t.e = &e;
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

    e.zero_r(reg::kRax);
    e.add_rsp_imm8(8);
    e.ret();

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
