#include <kernel/ps3/ppu.hpp>
#include <kernel/ps3/powerpc/decode.hpp>
#include <kernel/log.hpp>

namespace notyvos::ps3::ppu {

namespace {

using powerpc::Instruction;
using powerpc::Form;

// XER bits
constexpr u32 kXerSO = 1u << 31;

// CR field comparison values
constexpr u32 kCrLT = 0x8;
constexpr u32 kCrGT = 0x4;
constexpr u32 kCrEQ = 0x2;
constexpr u32 kCrSO = 0x1;

inline bool fetch32(Context* c, u64 addr, u32* out) noexcept {
    if (!c->read32) return false;
    return c->read32(c->user, addr, out);
}

inline void set_cr_from_cmp(Context* c, u32 field, i64 a, i64 b) noexcept {
    u32 v = 0;
    if (a < b)       v = kCrLT;
    else if (a > b)  v = kCrGT;
    else             v = kCrEQ;
    if (c->xer & kXerSO) v |= kCrSO;
    cr_set_field(c, field, v);
}

// Sign helpers
inline i32 as_i32(u64 v) noexcept { return static_cast<i32>(v & 0xFFFFFFFFu); }
inline i64 as_i64(u64 v) noexcept { return static_cast<i64>(v); }

} // namespace

void init(Context* ctx) noexcept {
    for (u32 i = 0; i < kGprCount; ++i) ctx->gpr[i] = 0;
    for (u32 i = 0; i < kFprCount; ++i) ctx->fpr[i] = 0;
    ctx->pc = 0;
    ctx->lr = 0;
    ctx->ctr = 0;
    ctx->xer = 0;
    ctx->cr = 0;
    ctx->read8 = nullptr;
    ctx->read16 = nullptr;
    ctx->read32 = nullptr;
    ctx->read64 = nullptr;
    ctx->write8 = nullptr;
    ctx->write16 = nullptr;
    ctx->write32 = nullptr;
    ctx->write64 = nullptr;
    ctx->syscall = nullptr;
    ctx->user = nullptr;
}

bool step(Context* ctx) noexcept {
    u32 word = 0;
    if (!fetch32(ctx, ctx->pc, &word)) {
        log::write(log::Level::Warn, "ppu",
            "instruction fetch failed at pc=0x%llx",
            static_cast<unsigned long long>(ctx->pc));
        return false;
    }

    const Instruction ins = powerpc::decode(word);
    const u32 rt = ins.rt;
    const u32 ra = ins.ra;
    const u32 rb = ins.rb;

    switch (ins.opcode) {
        // ---------------- D-form arithmetic ----------------
        case 14: {   // addi
            ctx->gpr[rt] = static_cast<u64>(
                static_cast<i64>(ins.simm) +
                ((ra == 0) ? 0 : as_i64(ctx->gpr[ra])));
            ctx->pc += 4;
            return true;
        }
        case 15: {   // addis
            const i64 imm = static_cast<i64>(ins.simm) << 16;
            ctx->gpr[rt] = static_cast<u64>(
                imm + ((ra == 0) ? 0 : as_i64(ctx->gpr[ra])));
            ctx->pc += 4;
            return true;
        }
        case 24: {   // ori
            ctx->gpr[ra] = ctx->gpr[rt] | ins.uimm;
            ctx->pc += 4;
            return true;
        }
        case 25: {   // oris
            ctx->gpr[ra] = ctx->gpr[rt] | (static_cast<u64>(ins.uimm) << 16);
            ctx->pc += 4;
            return true;
        }
        case 26: {   // xori
            ctx->gpr[ra] = ctx->gpr[rt] ^ ins.uimm;
            ctx->pc += 4;
            return true;
        }
        case 27: {   // xoris
            ctx->gpr[ra] = ctx->gpr[rt] ^ (static_cast<u64>(ins.uimm) << 16);
            ctx->pc += 4;
            return true;
        }
        case 28: {   // andi.
            ctx->gpr[ra] = ctx->gpr[rt] & ins.uimm;
            set_cr_from_cmp(ctx, 0, as_i64(ctx->gpr[ra]), 0);
            ctx->pc += 4;
            return true;
        }
        case 29: {   // andis.
            ctx->gpr[ra] = ctx->gpr[rt] & (static_cast<u64>(ins.uimm) << 16);
            set_cr_from_cmp(ctx, 0, as_i64(ctx->gpr[ra]), 0);
            ctx->pc += 4;
            return true;
        }

        // ---------------- D-form loads / stores ----------------
        case 32: {   // lwz
            const u64 addr = ((ra == 0) ? 0 : ctx->gpr[ra]) +
                             static_cast<u64>(ins.simm);
            u32 v = 0;
            if (!ctx->read32 || !ctx->read32(ctx->user, addr, &v)) return false;
            ctx->gpr[rt] = v;
            ctx->pc += 4;
            return true;
        }
        case 34: {   // lbz
            const u64 addr = ((ra == 0) ? 0 : ctx->gpr[ra]) +
                             static_cast<u64>(ins.simm);
            u8 v = 0;
            if (!ctx->read8 || !ctx->read8(ctx->user, addr, &v)) return false;
            ctx->gpr[rt] = v;
            ctx->pc += 4;
            return true;
        }
        case 36: {   // stw
            const u64 addr = ((ra == 0) ? 0 : ctx->gpr[ra]) +
                             static_cast<u64>(ins.simm);
            if (!ctx->write32 ||
                !ctx->write32(ctx->user, addr, static_cast<u32>(ctx->gpr[rt]))) {
                return false;
            }
            ctx->pc += 4;
            return true;
        }
        case 38: {   // stb
            const u64 addr = ((ra == 0) ? 0 : ctx->gpr[ra]) +
                             static_cast<u64>(ins.simm);
            if (!ctx->write8 ||
                !ctx->write8(ctx->user, addr, static_cast<u8>(ctx->gpr[rt]))) {
                return false;
            }
            ctx->pc += 4;
            return true;
        }
        case 40: {   // lhz
            const u64 addr = ((ra == 0) ? 0 : ctx->gpr[ra]) +
                             static_cast<u64>(ins.simm);
            u16 v = 0;
            if (!ctx->read16 || !ctx->read16(ctx->user, addr, &v)) return false;
            ctx->gpr[rt] = v;
            ctx->pc += 4;
            return true;
        }
        case 44: {   // sth
            const u64 addr = ((ra == 0) ? 0 : ctx->gpr[ra]) +
                             static_cast<u64>(ins.simm);
            if (!ctx->write16 ||
                !ctx->write16(ctx->user, addr, static_cast<u16>(ctx->gpr[rt]))) {
                return false;
            }
            ctx->pc += 4;
            return true;
        }

        // ---------------- Branch ----------------
        case 18: {   // b / bl
            const u64 target = ctx->pc + static_cast<u64>(ins.bdisp);
            if (ins.lk) ctx->lr = ctx->pc + 4;
            ctx->pc = target;
            return true;
        }
        case 16: {   // bc
            const u32 bo = (word >> 21) & 0x1Fu;
            const u32 bi = (word >> 16) & 0x1Fu;
            const bool ctr_ok = ((bo & 0x4) != 0) ||
                                (((ctx->ctr = ctx->ctr - 1) & 0xFFFFFFFFu) != 0) == ((bo & 0x2) != 0);
            const u32 crbit = (ctx->cr >> (31u - bi)) & 1u;
            const bool cr_ok  = ((bo & 0x10) != 0) || (crbit == ((bo & 0x8) != 0 ? 1u : 0u));
            if (ctr_ok && cr_ok) {
                ctx->pc += static_cast<u64>(ins.bdisp);
            } else {
                ctx->pc += 4;
            }
            if (ins.lk) ctx->lr = ctx->pc;
            return true;
        }
        case 19: {   // bclr / bcctr
            if (ins.xo == 16) {          // bclr
                const u64 target = ctx->lr & ~3ull;
                if (ins.lk) ctx->lr = ctx->pc + 4;
                ctx->pc = target;
                return true;
            }
            if (ins.xo == 528) {         // bcctr
                const u64 target = ctx->ctr & ~3ull;
                if (ins.lk) ctx->lr = ctx->pc + 4;
                ctx->pc = target;
                return true;
            }
            log::write(log::Level::Warn, "ppu",
                "unhandled XL xo=0x%llx at pc=0x%llx",
                static_cast<unsigned long long>(ins.xo),
                static_cast<unsigned long long>(ctx->pc));
            return false;
        }

        // ---------------- Primary-31 (X / XO / XFX) ----------------
        case 31: {
            switch (ins.xo) {
                case 266: {  // add
                    ctx->gpr[rt] = static_cast<u64>(
                        as_i64(ctx->gpr[ra]) + as_i64(ctx->gpr[rb]));
                    ctx->pc += 4;
                    return true;
                }
                case 40: {   // subf
                    ctx->gpr[rt] = static_cast<u64>(
                        as_i64(ctx->gpr[rb]) - as_i64(ctx->gpr[ra]));
                    ctx->pc += 4;
                    return true;
                }
                case 235: {  // mullw
                    ctx->gpr[rt] = static_cast<u64>(
                        as_i32(ctx->gpr[ra]) * as_i32(ctx->gpr[rb]));
                    ctx->pc += 4;
                    return true;
                }
                case 28: {   // and
                    ctx->gpr[ra] = ctx->gpr[rt] & ctx->gpr[rb];
                    ctx->pc += 4;
                    return true;
                }
                case 444: {  // or
                    ctx->gpr[ra] = ctx->gpr[rt] | ctx->gpr[rb];
                    ctx->pc += 4;
                    return true;
                }
                case 316: {  // xor
                    ctx->gpr[ra] = ctx->gpr[rt] ^ ctx->gpr[rb];
                    ctx->pc += 4;
                    return true;
                }
                case 476: {  // nand
                    ctx->gpr[ra] = ~(ctx->gpr[rt] & ctx->gpr[rb]);
                    ctx->pc += 4;
                    return true;
                }
                case 124: {  // nor
                    ctx->gpr[ra] = ~(ctx->gpr[rt] | ctx->gpr[rb]);
                    ctx->pc += 4;
                    return true;
                }
                case 339: {  // mfspr
                    u64 v = 0;
                    switch (ins.spr) {
                        case 8:  v = ctx->lr;   break;
                        case 9:  v = ctx->ctr;  break;
                        case 1:  v = ctx->xer;  break;
                        default: break;
                    }
                    ctx->gpr[rt] = v;
                    ctx->pc += 4;
                    return true;
                }
                case 467: {  // mtspr
                    const u64 v = ctx->gpr[rt];
                    switch (ins.spr) {
                        case 8:  ctx->lr   = v; break;
                        case 9:  ctx->ctr  = v; break;
                        case 1:  ctx->xer  = v; break;
                        default: break;
                    }
                    ctx->pc += 4;
                    return true;
                }
                case 0: {    // cmp
                    const u32 crf = (word >> 23) & 0x7u;
                    set_cr_from_cmp(ctx, crf,
                        as_i64(ctx->gpr[ra]), as_i64(ctx->gpr[rb]));
                    ctx->pc += 4;
                    return true;
                }
                case 32: {   // cmpl
                    const u32 crf = (word >> 23) & 0x7u;
                    const u64 a = ctx->gpr[ra];
                    const u64 b = ctx->gpr[rb];
                    u32 v = (a < b) ? kCrLT : ((a > b) ? kCrGT : kCrEQ);
                    if (ctx->xer & kXerSO) v |= kCrSO;
                    cr_set_field(ctx, crf, v);
                    ctx->pc += 4;
                    return true;
                }
                case 24: {   // slw
                    const u32 sh = static_cast<u32>(ctx->gpr[rb]) & 0x3Fu;
                    ctx->gpr[ra] = (sh & 0x20u)
                                 ? 0
                                 : (ctx->gpr[rt] << sh);
                    ctx->pc += 4;
                    return true;
                }
                case 536: {  // srw
                    const u32 sh = static_cast<u32>(ctx->gpr[rb]) & 0x3Fu;
                    ctx->gpr[ra] = (sh & 0x20u)
                                 ? 0
                                 : (ctx->gpr[rt] & 0xFFFFFFFFu) >> sh;
                    ctx->pc += 4;
                    return true;
                }
                case 792: {  // sraw
                    const u32 sh = static_cast<u32>(ctx->gpr[rb]) & 0x3Fu;
                    ctx->gpr[ra] = static_cast<u64>(
                        as_i32(ctx->gpr[rt]) >> sh);
                    ctx->pc += 4;
                    return true;
                }
                default:
                    log::write(log::Level::Warn, "ppu",
                        "unhandled opcode 31 xo=0x%llx at pc=0x%llx (word=0x%llx)",
                        static_cast<unsigned long long>(ins.xo),
                        static_cast<unsigned long long>(ctx->pc),
                        static_cast<unsigned long long>(word));
                    return false;
            }
        }

        case 17: {   // sc -- syscall
            if (!ctx->syscall || !ctx->syscall(ctx->user, ctx)) {
                log::write(log::Level::Warn, "ppu",
                    "unhandled syscall, r3=0x%llx pc=0x%llx",
                    static_cast<unsigned long long>(ctx->gpr[3]),
                    static_cast<unsigned long long>(ctx->pc));
                return false;
            }
            ctx->pc += 4;
            return true;
        }

        default:
            log::write(log::Level::Warn, "ppu",
                "unhandled opcode %llu at pc=0x%llx (word=0x%llx)",
                static_cast<unsigned long long>(ins.opcode),
                static_cast<unsigned long long>(ctx->pc),
                static_cast<unsigned long long>(word));
            return false;
    }
}

u64 run(Context* ctx, u64 max_steps) noexcept {
    u64 steps = 0;
    while (steps < max_steps) {
        if (!step(ctx)) break;
        ++steps;
    }
    return steps;
}

} // namespace notyvos::ps3::ppu