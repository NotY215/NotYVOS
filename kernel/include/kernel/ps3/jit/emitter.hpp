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
constexpr u8 kR8  = 8;
constexpr u8 kR9  = 9;
constexpr u8 kR10 = 10;
constexpr u8 kR11 = 11;
constexpr u8 kR12 = 12;
constexpr u8 kR13 = 13;
constexpr u8 kR14 = 14;
constexpr u8 kR15 = 15;
} // namespace reg

namespace alu
{
constexpr u8 kAdd = 0;
constexpr u8 kOr  = 1;
constexpr u8 kAnd = 4;
constexpr u8 kSub = 5;
constexpr u8 kXor = 6;
constexpr u8 kCmp = 7;
} // namespace alu

namespace cc
{
constexpr u8 kE  = 0x4;
constexpr u8 kNe = 0x5;
constexpr u8 kL  = 0xC;
constexpr u8 kGe = 0xD;
constexpr u8 kLe = 0xE;
constexpr u8 kG  = 0xF;
} // namespace cc

class Emitter
{
public:
    Emitter(u8* buf, usize cap) noexcept;

    void emit_u8(u8 v) noexcept;
    void emit_u32(u32 v) noexcept;
    void emit_u64(u64 v) noexcept;

    // 64-bit register moves.
    void mov_rr(u8 dst, u8 src) noexcept;
    void mov_ri64(u8 dst, u64 imm) noexcept;
    void mov_ri32(u8 dst, u32 imm) noexcept;

    // 64-bit memory moves.
    void mov_rm(u8 dst, u8 base, i32 disp) noexcept;
    void mov_mr(u8 base, i32 disp, u8 src) noexcept;

    // 32-bit memory moves (upper halves zeroed).
    void mov_r32_mem(u8 dst, u8 base, i32 disp) noexcept;
    void mov_mem_r32(u8 base, i32 disp, u8 src) noexcept;

    // ALU forms.
    void alu_rr(u8 op, u8 dst, u8 src) noexcept;
    void alu_ri32(u8 op, u8 dst, i32 imm) noexcept;
    void alu_mem_imm8(u8 op, u8 base, i32 disp, u8 imm8) noexcept;

    // Zeroing.
    void zero_r(u8 dst) noexcept;

    // Stack.
    void sub_rsp_imm8(u8 imm8) noexcept;
    void add_rsp_imm8(u8 imm8) noexcept;

    // Shifts by imm8.
    void shl_ri8_64(u8 dst, u8 imm8) noexcept;
    void sar_ri8_64(u8 dst, u8 imm8) noexcept;
    void sar_ri8_32(u8 dst, u8 imm8) noexcept;

    void call_r(u8 reg) noexcept;
    void ret() noexcept;

    u32 jmp_placeholder() noexcept;
    u32 jcc_placeholder(u8 condition) noexcept;
    void patch_branch_to_here(u32 disp_offset) noexcept;

    bool overflowed() const noexcept { return overflow_; }
    usize size() const noexcept { return pos_; }
    u8* data() noexcept { return buf_; }
    u32 here() const noexcept { return static_cast<u32>(pos_); }

private:
    void ensure(usize n) noexcept;

    u8* buf_;
    usize cap_;
    usize pos_;
    bool overflow_;
};

} // namespace notyvos::ps3::jit