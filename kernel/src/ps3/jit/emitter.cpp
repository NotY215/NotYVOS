#include <kernel/ps3/jit/emitter.hpp>

namespace notyvos::ps3::jit
{

namespace
{
inline u8 rex(bool w, u8 r, u8 x, u8 b) noexcept
{
    return static_cast<u8>(0x40 | (w ? 0x08 : 0x00) | (((r >> 3) & 1u) << 2) |
                           (((x >> 3) & 1u) << 1) | ((b >> 3) & 1u));
}

// ModRM mod field is the top two bits:
//   0x00 -> mod=00, memory operand [rm]
//   0x40 -> mod=01, memory operand [rm + disp8]
//   0x80 -> mod=10, memory operand [rm + disp32]
//   0xC0 -> mod=11, register operand  rm
constexpr u8 kModRM_RegInd = 0xC0;    // mod=11, rm is a register
constexpr u8 kModRM_MemDisp32 = 0x80; // mod=10, rm is a base register + disp32

static_assert((kModRM_RegInd & 0xC0u) == 0xC0u, "ModRM RegInd must be mod=11");
static_assert((kModRM_MemDisp32 & 0xC0u) == 0x80u, "ModRM MemDisp32 must be mod=10");
} // namespace

Emitter::Emitter(u8* buf, usize cap) noexcept : buf_(buf), cap_(cap), pos_(0), overflow_(false) {}

void Emitter::ensure(usize n) noexcept
{
    if (pos_ + n > cap_)
        overflow_ = true;
}

void Emitter::emit_u8(u8 v) noexcept
{
    if (overflow_)
        return;
    ensure(1);
    if (overflow_)
        return;
    buf_[pos_++] = v;
}

void Emitter::emit_u32(u32 v) noexcept
{
    if (overflow_)
        return;
    ensure(4);
    if (overflow_)
        return;
    for (u32 i = 0; i < 4; ++i)
        buf_[pos_++] = static_cast<u8>((v >> (i * 8)) & 0xFFu);
}

void Emitter::emit_u64(u64 v) noexcept
{
    if (overflow_)
        return;
    ensure(8);
    if (overflow_)
        return;
    for (u32 i = 0; i < 8; ++i)
        buf_[pos_++] = static_cast<u8>((v >> (i * 8)) & 0xFFu);
}

// mov r/m64, r64  (89 /r)
void Emitter::mov_rr(u8 dst, u8 src) noexcept
{
    emit_u8(rex(true, src, 0, dst));
    emit_u8(0x89);
    emit_u8(static_cast<u8>(kModRM_RegInd | ((src & 7u) << 3) | (dst & 7u)));
}

// movabs r64, imm64  (REX.W B8+rd)
void Emitter::mov_ri64(u8 dst, u64 imm) noexcept
{
    emit_u8(rex(true, 0, 0, dst));
    emit_u8(static_cast<u8>(0xB8u | (dst & 7u)));
    emit_u64(imm);
}

// mov r64, [base + disp32]  (REX.W 8B /r, mod=10)
void Emitter::mov_rm(u8 dst, u8 base, i32 disp) noexcept
{
    emit_u8(rex(true, dst, 0, base));
    emit_u8(0x8B);
    emit_u8(static_cast<u8>(kModRM_MemDisp32 | ((dst & 7u) << 3) | (base & 7u)));
    emit_u32(static_cast<u32>(disp));
}

// mov [base + disp32], r64  (REX.W 89 /r, mod=10)
void Emitter::mov_mr(u8 base, i32 disp, u8 src) noexcept
{
    emit_u8(rex(true, src, 0, base));
    emit_u8(0x89);
    emit_u8(static_cast<u8>(kModRM_MemDisp32 | ((src & 7u) << 3) | (base & 7u)));
    emit_u32(static_cast<u32>(disp));
}

// op r/m64, r64  (01 /r add, 09 /r or, 21 /r and, 29 /r sub, 31 /r xor,
// 39 /r cmp). We always emit mod=11, so ModRM.reg=src, ModRM.rm=dst.
void Emitter::alu_rr(u8 op, u8 dst, u8 src) noexcept
{
    u8 opcode = 0;
    switch (op)
    {
    case alu::kAdd:
        opcode = 0x01;
        break;
    case alu::kOr:
        opcode = 0x09;
        break;
    case alu::kAnd:
        opcode = 0x21;
        break;
    case alu::kSub:
        opcode = 0x29;
        break;
    case alu::kXor:
        opcode = 0x31;
        break;
    case alu::kCmp:
        opcode = 0x39;
        break;
    default:
        opcode = 0x01;
        break;
    }
    emit_u8(rex(true, src, 0, dst));
    emit_u8(opcode);
    emit_u8(static_cast<u8>(kModRM_RegInd | ((src & 7u) << 3) | (dst & 7u)));
}

// op r64, imm32  (REX.W 81 /digit id, mod=11)
void Emitter::alu_ri32(u8 op, u8 dst, i32 imm) noexcept
{
    emit_u8(rex(true, 0, 0, dst));
    emit_u8(0x81);
    emit_u8(static_cast<u8>(kModRM_RegInd | ((op & 7u) << 3) | (dst & 7u)));
    emit_u32(static_cast<u32>(imm));
}

// op qword [base + disp32], imm8  (REX.W 83 /digit ib, mod=10)
void Emitter::alu_mem_imm8(u8 op, u8 base, i32 disp, u8 imm8) noexcept
{
    emit_u8(rex(true, 0, 0, base));
    emit_u8(0x83);
    emit_u8(static_cast<u8>(kModRM_MemDisp32 | ((op & 7u) << 3) | (base & 7u)));
    emit_u32(static_cast<u32>(disp));
    emit_u8(imm8);
}

void Emitter::zero_r(u8 dst) noexcept
{
    // xor r32, r32  (zeroes the full 64-bit register)
    emit_u8(rex(false, dst, 0, dst));
    emit_u8(0x31);
    emit_u8(static_cast<u8>(kModRM_RegInd | ((dst & 7u) << 3) | (dst & 7u)));
}

void Emitter::ret() noexcept
{
    emit_u8(0xC3);
}

u32 Emitter::jmp_placeholder() noexcept
{
    emit_u8(0xE9);
    const u32 off = here();
    emit_u32(0);
    return off;
}

u32 Emitter::jcc_placeholder(u8 condition) noexcept
{
    emit_u8(0x0F);
    emit_u8(static_cast<u8>(0x80u | (condition & 0x0Fu)));
    const u32 off = here();
    emit_u32(0);
    return off;
}

void Emitter::patch_branch_to_here(u32 disp_offset) noexcept
{
    if (disp_offset + 4 > pos_)
        return;
    const i32 rel = static_cast<i32>(pos_) - static_cast<i32>(disp_offset + 4);
    const u32 u = static_cast<u32>(rel);
    buf_[disp_offset + 0] = static_cast<u8>(u & 0xFFu);
    buf_[disp_offset + 1] = static_cast<u8>((u >> 8) & 0xFFu);
    buf_[disp_offset + 2] = static_cast<u8>((u >> 16) & 0xFFu);
    buf_[disp_offset + 3] = static_cast<u8>((u >> 24) & 0xFFu);
}

} // namespace notyvos::ps3::jit
