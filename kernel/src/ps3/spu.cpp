#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/spu.hpp>

namespace notyvos::ps3::spu
{

namespace
{

// Synthetic SPU encoding, big-endian 32-bit on disk:
//   bits 31..25  opcode    (7)
//   bits 24..18  rt        (7)
//   bits 17..11  ra        (7)
//   bits 10..4   rb        (7)
//   bits 3..0    spare     (4, must be 0)
//
// Immediate format (opcode-dependent): the same word carries the 16-bit
// immediate at bits 17..2, i.e. `rt` and `imm16` coexist without overlap
// because `ra`/`rb` are not used by immediate ops.
//
// Opcodes:
//   0x00 halt            0x01 nop
//   0x10 a   (add)       0x11 sf (sub)   0x12 and   0x13 or   0x14 xor
//   0x20 il  (load imm)  0x21 wrch       0x22 rdch
//   0x23 dma_read        0x24 dma_write  0x25 stop
constexpr u32 kOpShift = 25;
constexpr u32 kOpMask = 0x7Fu;
constexpr u32 kRtShift = 18;
constexpr u32 kRtMask = 0x7Fu;
constexpr u32 kRaShift = 11;
constexpr u32 kRaMask = 0x7Fu;
constexpr u32 kRbShift = 4;
constexpr u32 kRbMask = 0x7Fu;
constexpr u32 kImmShift = 2;
constexpr u32 kImmMask = 0xFFFFu;

constexpr u32 kOpHalt = 0x00;
constexpr u32 kOpNop = 0x01;
constexpr u32 kOpAdd = 0x10;
constexpr u32 kOpSub = 0x11;
constexpr u32 kOpAnd = 0x12;
constexpr u32 kOpOr = 0x13;
constexpr u32 kOpXor = 0x14;
constexpr u32 kOpIl = 0x20;
constexpr u32 kOpWrch = 0x21;
constexpr u32 kOpRdch = 0x22;
constexpr u32 kOpDmaR = 0x23;
constexpr u32 kOpDmaW = 0x24;
constexpr u32 kOpStop = 0x25;

inline u32 load_be32(const u8* p) noexcept
{
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

inline bool fetch32(Context* ctx, u32 addr, u32* out) noexcept
{
    if (addr + 4 > kLocalStoreSize)
        return false;
    *out = load_be32(ctx->local_store + addr);
    return true;
}

inline u32 op_of(u32 w) noexcept
{
    return (w >> kOpShift) & kOpMask;
}
inline u32 rt_of(u32 w) noexcept
{
    return (w >> kRtShift) & kRtMask;
}
inline u32 ra_of(u32 w) noexcept
{
    return (w >> kRaShift) & kRaMask;
}
inline u32 rb_of(u32 w) noexcept
{
    return (w >> kRbShift) & kRbMask;
}
inline u32 imm_of(u32 w) noexcept
{
    return (w >> kImmShift) & kImmMask;
}

enum class Lop
{
    Add,
    Sub,
    And,
    Or,
    Xor
};

void lane_apply(Context* ctx, u32 rt, u32 ra, u32 rb, Lop op) noexcept
{
    for (u32 i = 0; i < 4; ++i)
    {
        const u32 a = ctx->gpr[ra][i];
        const u32 b = ctx->gpr[rb][i];
        u32 r = 0;
        switch (op)
        {
        case Lop::Add:
            r = a + b;
            break;
        case Lop::Sub:
            r = a - b;
            break;
        case Lop::And:
            r = a & b;
            break;
        case Lop::Or:
            r = a | b;
            break;
        case Lop::Xor:
            r = a ^ b;
            break;
        }
        ctx->gpr[rt][i] = r;
    }
}

void load_imm(Context* ctx, u32 rt, u32 imm) noexcept
{
    for (u32 i = 0; i < 4; ++i)
        ctx->gpr[rt][i] = imm;
}

} // namespace

void init(Context* ctx) noexcept
{
    for (u32 r = 0; r < kRegisterCount; ++r)
        for (u32 l = 0; l < 4; ++l)
            ctx->gpr[r][l] = 0;
    ctx->pc = 0;
    libk::memset(ctx->local_store, 0, kLocalStoreSize);
    ctx->spu_status = 0;
    ctx->spu_cfg = 0;
    ctx->lslr = 0x3FFFF;
    ctx->lsr = 0;
    ctx->inbound.head = ctx->inbound.tail = ctx->inbound.count = 0;
    ctx->outbound.head = ctx->outbound.tail = ctx->outbound.count = 0;
    ctx->event_mask = 0;
    ctx->stopped = false;
    ctx->halted = false;
    ctx->dma_read = nullptr;
    ctx->dma_write = nullptr;
    ctx->user = nullptr;
}

bool mailbox_push_inbound(Context* ctx, u32 value) noexcept
{
    if (ctx->inbound.count >= kMailboxDepth)
        return false;
    ctx->inbound.items[ctx->inbound.head] = value;
    ctx->inbound.head = (ctx->inbound.head + 1) % kMailboxDepth;
    ++ctx->inbound.count;
    return true;
}

bool mailbox_pop_outbound(Context* ctx, u32* out) noexcept
{
    if (ctx->outbound.count == 0)
        return false;
    *out = ctx->outbound.items[ctx->outbound.tail];
    ctx->outbound.tail = (ctx->outbound.tail + 1) % kMailboxDepth;
    --ctx->outbound.count;
    return true;
}

bool ls_read32(const Context* ctx, u32 addr, u32* out) noexcept
{
    if (addr + 4 > kLocalStoreSize)
        return false;
    *out = load_be32(ctx->local_store + addr);
    return true;
}

bool ls_write32(Context* ctx, u32 addr, u32 value) noexcept
{
    if (addr + 4 > kLocalStoreSize)
        return false;
    ctx->local_store[addr + 0] = static_cast<u8>((value >> 24) & 0xFF);
    ctx->local_store[addr + 1] = static_cast<u8>((value >> 16) & 0xFF);
    ctx->local_store[addr + 2] = static_cast<u8>((value >> 8) & 0xFF);
    ctx->local_store[addr + 3] = static_cast<u8>(value & 0xFF);
    return true;
}

bool step(Context* ctx) noexcept
{
    if (ctx->halted || ctx->stopped)
        return false;
    u32 word = 0;
    if (!fetch32(ctx, ctx->pc, &word))
    {
        log::write(log::Level::Warn, "spu", "fetch failed at ls:0x%llx",
                   static_cast<unsigned long long>(ctx->pc));
        return false;
    }

    const u32 op = op_of(word);
    switch (op)
    {
    case kOpHalt:
        ctx->halted = true;
        return true;
    case kOpNop:
        ctx->pc += 4;
        return true;

    case kOpAdd:
        lane_apply(ctx, rt_of(word), ra_of(word), rb_of(word), Lop::Add);
        ctx->pc += 4;
        return true;
    case kOpSub:
        lane_apply(ctx, rt_of(word), ra_of(word), rb_of(word), Lop::Sub);
        ctx->pc += 4;
        return true;
    case kOpAnd:
        lane_apply(ctx, rt_of(word), ra_of(word), rb_of(word), Lop::And);
        ctx->pc += 4;
        return true;
    case kOpOr:
        lane_apply(ctx, rt_of(word), ra_of(word), rb_of(word), Lop::Or);
        ctx->pc += 4;
        return true;
    case kOpXor:
        lane_apply(ctx, rt_of(word), ra_of(word), rb_of(word), Lop::Xor);
        ctx->pc += 4;
        return true;

    case kOpIl:
        load_imm(ctx, rt_of(word), imm_of(word));
        ctx->pc += 4;
        return true;

    case kOpWrch:
    {
        const u32 ra = ra_of(word);
        const u32 v = ctx->gpr[ra][0];
        if (ctx->outbound.count < kMailboxDepth)
        {
            ctx->outbound.items[ctx->outbound.head] = v;
            ctx->outbound.head = (ctx->outbound.head + 1) % kMailboxDepth;
            ++ctx->outbound.count;
        }
        ctx->pc += 4;
        return true;
    }

    case kOpRdch:
    {
        if (ctx->inbound.count == 0)
            return true; // blocked
        const u32 v = ctx->inbound.items[ctx->inbound.tail];
        ctx->inbound.tail = (ctx->inbound.tail + 1) % kMailboxDepth;
        --ctx->inbound.count;
        load_imm(ctx, rt_of(word), v);
        ctx->pc += 4;
        return true;
    }

    case kOpDmaR:
    {
        if (!ctx->dma_read)
        {
            ctx->halted = true;
            return false;
        }
        const u32 ra = ra_of(word);
        const u32 rb = rb_of(word);
        const u32 main_addr_lo = ctx->gpr[ra][0];
        const u32 ls_addr = ctx->gpr[rb][0];
        const u32 size = imm_of(word);
        if (!ctx->dma_read(ctx->user, main_addr_lo, ls_addr, size, 0))
            return false;
        ctx->pc += 4;
        return true;
    }

    case kOpDmaW:
    {
        if (!ctx->dma_write)
        {
            ctx->halted = true;
            return false;
        }
        const u32 ra = ra_of(word);
        const u32 rb = rb_of(word);
        const u32 main_addr_lo = ctx->gpr[ra][0];
        const u32 ls_addr = ctx->gpr[rb][0];
        const u32 size = imm_of(word);
        if (!ctx->dma_write(ctx->user, main_addr_lo, ls_addr, size, 0))
            return false;
        ctx->pc += 4;
        return true;
    }

    case kOpStop:
        ctx->stopped = true;
        return true;

    default:
        log::write(log::Level::Warn, "spu", "unhandled op 0x%llx at ls:0x%llx (word=0x%llx)",
                   static_cast<unsigned long long>(op), static_cast<unsigned long long>(ctx->pc),
                   static_cast<unsigned long long>(word));
        return false;
    }
}

u64 run(Context* ctx, u64 max_steps) noexcept
{
    u64 steps = 0;
    while (steps < max_steps)
    {
        if (ctx->halted || ctx->stopped)
            break;
        if (!step(ctx))
            break;
        ++steps;
    }
    return steps;
}

} // namespace notyvos::ps3::spu
