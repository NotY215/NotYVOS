#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::jit
{

namespace reg
{
constexpr u8 kRax = 0;
constexpr u8 kRcx = 1;
constexpr u8 kRdx = 2;
constexpr u8 kRbx = 3;
constexpr u8 kRsp = 4;
constexpr u8 kRbp = 5;
constexpr u8 kRsi = 6;
constexpr u8 kRdi = 7;
constexpr u8 kR8 = 8;
constexpr u8 kR9 = 9;
constexpr u8 kR10 = 10;
constexpr u8 kR11 = 11;
constexpr u8 kR12 = 12;
constexpr u8 kR13 = 13;
constexpr u8 kR14 = 14;
constexpr u8 kR15 = 15;
} // namespace reg

// ALU opcodes used by the register-immediate and memory-immediate forms
// (the /digit field of the ModRM byte).
namespace alu
{
constexpr u8 kAdd = 0;
constexpr u8 kOr = 1;
constexpr u8 kAnd = 4;
constexpr u8 kSub = 5;
constexpr u8 kXor = 6;
constexpr u8 kCmp = 7;
} // namespace alu

// x86-64 conditional codes (the low nibble of the 0F 8x Jcc opcode).
namespace cc
{
constexpr u8 kE = 0x4; // equal / ZF=1
constexpr u8 kNe = 0x5;
constexpr u8 kL = 0xC; // signed less
constexpr u8 kGe = 0xD;
constexpr u8 kLe = 0xE;
constexpr u8 kG = 0xF;
} // namespace cc

// Minimal x86-64 emitter for the NOTYVOS baseline JIT.
//
// Buffer is caller-owned. Once the buffer would overflow, `overflowed()`
// returns true and every further emit becomes a no-op.
//
// Limitation: the memory-operand forms do not emit a SIB byte. Only
// base registers whose low 3 bits are != 4 (i.e. not RSP and not R12)
// are supported. The JIT uses R15 exclusively as the Context pointer,
// which is unaffected.
class Emitter
{
public:
    Emitter(u8* buf, usize cap) noexcept;

    void emit_u8(u8 v) noexcept;
    void emit_u32(u32 v) noexcept;
    void emit_u64(u64 v) noexcept;

    // mov dst, src
    void mov_rr(u8 dst, u8 src) noexcept;
    // movabs dst, imm64
    void mov_ri64(u8 dst, u64 imm) noexcept;
    // mov dst, [base + disp32]
    void mov_rm(u8 dst, u8 base, i32 disp) noexcept;
    // mov [base + disp32], src
    void mov_mr(u8 base, i32 disp, u8 src) noexcept;

    // add/sub/and/or/xor dst, src
    void alu_rr(u8 op, u8 dst, u8 src) noexcept;
    // add/sub/and/or/xor dst, imm32 (sign-extended to 64)
    void alu_ri32(u8 op, u8 dst, i32 imm) noexcept;
    // add/sub/and/or/xor qword [base + disp32], imm8
    void alu_mem_imm8(u8 op, u8 base, i32 disp, u8 imm8) noexcept;

    // xor dst, dst  (zeroes dst; cheaper than mov 0)
    void zero_r(u8 dst) noexcept;

    void ret() noexcept;

    // Unconditional jmp rel32; returns offset of the 4-byte displacement.
    u32 jmp_placeholder() noexcept;
    // Conditional jcc rel32; `condition` is one of the cc:: constants.
    u32 jcc_placeholder(u8 condition) noexcept;
    // Patch a previously-emitted rel32 branch to point at the current position.
    void patch_branch_to_here(u32 disp_offset) noexcept;

    bool overflowed() const noexcept
    {
        return overflow_;
    }
    usize size() const noexcept
    {
        return pos_;
    }
    u8* data() noexcept
    {
        return buf_;
    }
    u32 here() const noexcept
    {
        return static_cast<u32>(pos_);
    }

private:
    void ensure(usize n) noexcept;

    u8* buf_;
    usize cap_;
    usize pos_;
    bool overflow_;
};

} // namespace notyvos::ps3::jit
