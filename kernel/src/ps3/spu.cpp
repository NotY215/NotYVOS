#include <kernel/ps3/spu.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::ps3::spu {

namespace {

// SPU instruction encoding is big-endian 32-bit, like PowerPC but with a
// much smaller opcode set. The first 7 bits are the primary opcode.
inline u32 load_be32(const u8* p) noexcept {
    return (static_cast<u32>(p[0]) << 24)
         | (static_cast<u32>(p[1]) << 16)
         | (static_cast<u32>(p[2]) <<  8)
         |  static_cast<u32>(p[3]);
}

inline bool fetch32(Context* ctx, u32 addr, u32* out) noexcept {
    if (addr + 4 > kLocalStoreSize) return false;
    *out = load_be32(ctx->local_store + addr);
    return true;
}

inline u32 rb10(u32 word) noexcept { return (word >> 21) & 0x7Fu; }
inline u32 ra7(u32 word)  noexcept { return (word >> 14) & 0x7Fu; }
inline u32 rt7(u32 word)  noexcept { return (word >>  7) & 0x7Fu; }

// Simple ALU lane helpers operating on the 4 u32 lanes.
enum class Lop { Add, Sub, And, Or, Xor };

void lane_apply(Context* ctx, u32 rt, u32 ra, u32 rb, Lop op) noexcept {
    for (u32 i = 0; i < 4; ++i) {
        const u32 a = ctx->gpr[ra][i];
        const u32 b = ctx->gpr[rb][i];
        u32 r = 0;
        switch (op) {
            case Lop::Add: r = a + b; break;
            case Lop::Sub: r = a - b; break;
            case Lop::And: r = a & b; break;
            case Lop::Or:  r = a | b; break;
            case Lop::Xor: r = a ^ b; break;
        }
        ctx->gpr[rt][i] = r;
    }
}

// Set all four lanes to the same constant.
void load_imm(Context* ctx, u32 rt, u32 imm) noexcept {
    for (u32 i = 0; i < 4; ++i) ctx->gpr[rt][i] = imm;
}

} // namespace

void init(Context* ctx) noexcept {
    for (u32 r = 0; r < kRegisterCount; ++r)
        for (u32 l = 0; l < 4; ++l)
            ctx->gpr[r][l] = 0;
    ctx->pc = 0;
    libk::memset(ctx->local_store, 0, kLocalStoreSize);

    ctx->spu_status = 0;
    ctx->spu_cfg = 0;
    ctx->lslr = 0x3FFFF;    // 256 KiB - 1
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

bool mailbox_push_inbound(Context* ctx, u32 value) noexcept {
    if (ctx->inbound.count >= kMailboxDepth) return false;
    ctx->inbound.items[ctx->inbound.head] = value;
    ctx->inbound.head = (ctx->inbound.head + 1) % kMailboxDepth;
    ++ctx->inbound.count;
    return true;
}

bool mailbox_pop_outbound(Context* ctx, u32* out) noexcept {
    if (ctx->outbound.count == 0) return false;
    *out = ctx->outbound.items[ctx->outbound.tail];
    ctx->outbound.tail = (ctx->outbound.tail + 1) % kMailboxDepth;
    --ctx->outbound.count;
    return true;
}

bool ls_read32(const Context* ctx, u32 addr, u32* out) noexcept {
    if (addr + 4 > kLocalStoreSize) return false;
    *out = load_be32(ctx->local_store + addr);
    return true;
}

bool ls_write32(Context* ctx, u32 addr, u32 value) noexcept {
    if (addr + 4 > kLocalStoreSize) return false;
    ctx->local_store[addr + 0] = static_cast<u8>((value >> 24) & 0xFF);
    ctx->local_store[addr + 1] = static_cast<u8>((value >> 16) & 0xFF);
    ctx->local_store[addr + 2] = static_cast<u8>((value >>  8) & 0xFF);
    ctx->local_store[addr + 3] = static_cast<u8>( value        & 0xFF);
    return true;
}

bool step(Context* ctx) noexcept {
    if (ctx->halted || ctx->stopped) return false;

    u32 word = 0;
    if (!fetch32(ctx, ctx->pc, &word)) {
        log::write(log::Level::Warn, "spu",
            "fetch failed at ls:0x%llx",
            static_cast<unsigned long long>(ctx->pc));
        return false;
    }

    const u32 op = (word >> 21) & 0x7FFu;   // 11-bit opcode for SPU

    switch (op) {
        // Synthetic 0x001 = halt (implementation-defined convenience)
        case 0x001: {
            ctx->halted = true;
            return true;
        }

        // Synthetic 0x002 = nop
        case 0x002: {
            ctx->pc += 4;
            return true;
        }

        // Synthetic 0x100 = a (add word)
        case 0x100: {
            const u32 rt = rt7(word);
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            lane_apply(ctx, rt, ra, rb, Lop::Add);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x101 = sf (subtract word)
        case 0x101: {
            const u32 rt = rt7(word);
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            lane_apply(ctx, rt, ra, rb, Lop::Sub);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x102 = and
        case 0x102: {
            const u32 rt = rt7(word);
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            lane_apply(ctx, rt, ra, rb, Lop::And);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x103 = or
        case 0x103: {
            const u32 rt = rt7(word);
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            lane_apply(ctx, rt, ra, rb, Lop::Or);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x104 = xor
        case 0x104: {
            const u32 rt = rt7(word);
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            lane_apply(ctx, rt, ra, rb, Lop::Xor);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x105 = il (load word immediate)
        case 0x105: {
            const u32 rt = rt7(word);
            const u32 imm = word & 0xFFFFu;
            load_imm(ctx, rt, imm);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x106 = wrch (write channel — outbound mailbox for ch 21)
        case 0x106: {
            const u32 ra = ra7(word);
            const u32 v = ctx->gpr[ra][0];
            if (ctx->outbound.count < kMailboxDepth) {
                ctx->outbound.items[ctx->outbound.head] = v;
                ctx->outbound.head = (ctx->outbound.head + 1) % kMailboxDepth;
                ++ctx->outbound.count;
            }
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x107 = rdch (read channel — inbound mailbox for ch 21)
        case 0x107: {
            const u32 rt = rt7(word);
            if (ctx->inbound.count == 0) {
                // Blocked: leave PC unchanged, return true so the caller
                // can yield. The scheduler treats "no progress" as a yield.
                return true;
            }
            const u32 v = ctx->inbound.items[ctx->inbound.tail];
            ctx->inbound.tail = (ctx->inbound.tail + 1) % kMailboxDepth;
            --ctx->inbound.count;
            load_imm(ctx, rt, v);
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x108 = dma_read (main -> local)
        case 0x108: {
            if (!ctx->dma_read) { ctx->halted = true; return false; }
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            const u32 main_addr_lo = ctx->gpr[ra][0];
            const u32 ls_addr      = ctx->gpr[rb][0];
            const u32 size         = (word & 0x7FFFu) << 4;
            const u32 tag          = 0;
            if (!ctx->dma_read(ctx->user, main_addr_lo, ls_addr, size, tag)) {
                return false;
            }
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x109 = dma_write (local -> main)
        case 0x109: {
            if (!ctx->dma_write) { ctx->halted = true; return false; }
            const u32 ra = ra7(word);
            const u32 rb = rb10(word);
            const u32 main_addr_lo = ctx->gpr[ra][0];
            const u32 ls_addr      = ctx->gpr[rb][0];
            const u32 size         = (word & 0x7FFFu) << 4;
            const u32 tag          = 0;
            if (!ctx->dma_write(ctx->user, main_addr_lo, ls_addr, size, tag)) {
                return false;
            }
            ctx->pc += 4;
            return true;
        }
        // Synthetic 0x10A = stop
        case 0x10A: {
            ctx->stopped = true;
            return true;
        }

        default:
            log::write(log::Level::Warn, "spu",
                "unhandled op 0x%llx at ls:0x%llx (word=0x%llx)",
                static_cast<unsigned long long>(op),
                static_cast<unsigned long long>(ctx->pc),
                static_cast<unsigned long long>(word));
            return false;
    }
}

u64 run(Context* ctx, u64 max_steps) noexcept {
    u64 steps = 0;
    while (steps < max_steps) {
        if (ctx->halted || ctx->stopped) break;
        if (!step(ctx)) break;
        ++steps;
    }
    return steps;
}

} // namespace notyvos::ps3::spu