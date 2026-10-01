#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/rsx/rsx.hpp>

namespace notyvos::ps3::rsx
{

namespace
{
// Register file: 4096 x u32 = 16 KB.
u32 g_regs[kRegisterCount] = {};

Fifo g_fifo = {};

Surface g_target = {};

u64 g_commands  = 0;
u64 g_draws     = 0;
u64 g_presents  = 0;
u64 g_clears    = 0;
u64 g_unknowns  = 0;
bool g_ready    = false;

// Rate-limited unknown-method logging: emit at most 8 warnings per boot,
// then stay silent so a game spamming an unimplemented method does not
// drown the serial console.
constexpr u32 kMaxUnknownLogs = 8;
u32 g_unknown_logs = 0;

inline u32 next_index(u32 idx) noexcept
{
    return (idx + 1u) % kFifoWords;
}

inline u32 fifo_used() noexcept
{
    const u32 p = g_fifo.put;
    const u32 g = g_fifo.get;
    return (p >= g) ? (p - g) : (kFifoWords - (g - p));
}

// Read one word from the FIFO, honouring any pending jump. Returns false
// if the FIFO is empty.
bool fifo_pop(u32& out) noexcept
{
    if (g_fifo.jump_pending)
    {
        g_fifo.get          = g_fifo.jump_target;
        g_fifo.jump_pending = false;
    }
    if (g_fifo.put == g_fifo.get)
        return false;
    out = g_fifo.buffer[g_fifo.get];
    g_fifo.get = next_index(g_fifo.get);
    ++g_fifo.total_words_out;
    return true;
}

// Peek without consuming. Returns false if the FIFO is empty.
bool fifo_peek(u32& out) noexcept
{
    if (g_fifo.jump_pending)
    {
        if (g_fifo.jump_target == g_fifo.put)
            return false;
        out = g_fifo.buffer[g_fifo.jump_target];
        return true;
    }
    if (g_fifo.put == g_fifo.get)
        return false;
    out = g_fifo.buffer[g_fifo.get];
    return true;
}

// --- Method handlers -------------------------------------------------------
//
// A method receives `data`, an array of 1..4 words already pulled from
// the FIFO, and `count`, its length.

void handle_nop(u32 /*count*/, const u32* /*data*/) noexcept
{
    // Nothing.
}

void handle_object(u32 count, const u32* data) noexcept
{
    // The RSX object model binds a state object to a channel. We log the
    // class ID on the first few to help future games compatibility work,
    // but accept and ignore.
    if (count >= 1 && g_unknown_logs < 2)
    {
        log::write(log::Level::Info, "rsx", "object class=0x%llx",
                   static_cast<unsigned long long>(data[0]));
    }
}

void handle_surface_format(u32 count, const u32* data) noexcept
{
    if (count < 1) return;
    g_target.format = static_cast<SurfaceFormat>(data[0] & 0xFFu);
    g_target.valid  = false;
}

void handle_surface_col(u32 count, const u32* data) noexcept
{
    if (count < 2) return;
    const u64 lo = static_cast<u64>(data[0]);
    const u64 hi = static_cast<u64>(data[1]);
    g_target.address = (hi << 32) | lo;
    g_target.valid   = false;
}

void handle_surface_pitch(u32 count, const u32* data) noexcept
{
    if (count < 1) return;
    g_target.pitch  = data[0] & 0x0000FFFFu;
    g_target.valid  = false;
}

void handle_surface_w(u32 count, const u32* data) noexcept
{
    if (count < 1) return;
    g_target.width = data[0] & 0x0000FFFFu;
}

void handle_surface_h(u32 count, const u32* data) noexcept
{
    if (count < 1) return;
    g_target.height = data[0] & 0x0000FFFFu;
    if (g_target.width != 0u && g_target.height != 0u &&
        g_target.pitch != 0u && g_target.address != 0u)
        g_target.valid = true;
}

void handle_clear_color(u32 count, const u32* data) noexcept
{
    for (u32 i = 0; i < count && i < 4; ++i)
        g_regs[(method::kClearColor / 4) + i] = data[i];
}

void handle_clear_depth(u32 count, const u32* data) noexcept
{
    if (count >= 1)
        g_regs[method::kClearDepth / 4] = data[0];
}

void handle_clear(u32 /*count*/, const u32* /*data*/) noexcept
{
    ++g_clears;
    // Part 2 will call into the Graphics HAL to actually clear the
    // surface this RSX is targeting.
}

void handle_draw(u32 /*count*/, const u32* /*data*/) noexcept
{
    ++g_draws;
}

void handle_present(u32 /*count*/, const u32* /*data*/) noexcept
{
    ++g_presents;
}

void handle_jump(u32 count, const u32* data) noexcept
{
    if (count < 1) return;
    g_fifo.jump_target  = data[0] % kFifoWords;
    g_fifo.jump_pending = true;
}

void handle_call(u32 count, const u32* data) noexcept
{
    if (count < 1) return;
    if (g_fifo.call_sp < kCallDepth)
    {
        g_fifo.call_stack[g_fifo.call_sp++] = g_fifo.get;
    }
    g_fifo.jump_target  = data[0] % kFifoWords;
    g_fifo.jump_pending = true;
}

void handle_return(u32 /*count*/, const u32* /*data*/) noexcept
{
    if (g_fifo.call_sp == 0)
        return;
    g_fifo.jump_target  = g_fifo.call_stack[--g_fifo.call_sp];
    g_fifo.jump_pending = true;
}

void handle_unknown(u32 byte_offset) noexcept
{
    ++g_unknowns;
    if (g_unknown_logs < kMaxUnknownLogs)
    {
        ++g_unknown_logs;
        log::write(log::Level::Warn, "rsx",
                   "unknown method 0x%llx (rate-limited)",
                   static_cast<unsigned long long>(byte_offset));
    }
}

using Handler = void (*)(u32, const u32*);

Handler handler_for(u32 byte_offset) noexcept
{
    switch (byte_offset)
    {
    case method::kNop:        return &handle_nop;
    case method::kObject:     return &handle_object;
    case method::kSurfaceFmt: return &handle_surface_format;
    case method::kSurfaceCol: return &handle_surface_col;
    case method::kSurfacePit: return &handle_surface_pitch;
    case method::kSurfaceW:   return &handle_surface_w;
    case method::kSurfaceH:   return &handle_surface_h;
    case method::kClearColor: return &handle_clear_color;
    case method::kClearDepth: return &handle_clear_depth;
    case method::kClear:      return &handle_clear;
    case method::kDraw:       return &handle_draw;
    case method::kPresent:    return &handle_present;
    case method::kFifoJump:   return &handle_jump;
    case method::kFifoCall:   return &handle_call;
    case method::kFifoReturn: return &handle_return;
    default:                  return nullptr;
    }
}

} // namespace

void Rsx::init() noexcept
{
    libk::memset(g_regs, 0, sizeof(g_regs));
    libk::memset(&g_fifo, 0, sizeof(g_fifo));
    libk::memset(&g_target, 0, sizeof(g_target));
    g_target.format = SurfaceFormat::Unknown;
    g_commands  = 0;
    g_draws     = 0;
    g_presents  = 0;
    g_clears    = 0;
    g_unknowns  = 0;
    g_unknown_logs = 0;
    g_ready = true;
    log::write(log::Level::Info, "rsx",
               "Phase 6A init: %llu regs, FIFO %llu words",
               static_cast<unsigned long long>(kRegisterCount),
               static_cast<unsigned long long>(kFifoWords));
}

bool Rsx::ready() noexcept { return g_ready; }

bool Rsx::push(u32 word) noexcept
{
    if (!g_ready)
        return false;
    const u32 next = next_index(g_fifo.put);
    if (next == g_fifo.get)
        return false; // full
    g_fifo.buffer[g_fifo.put] = word;
    g_fifo.put = next;
    ++g_fifo.total_words_in;
    return true;
}

u32 Rsx::process(u32 max_commands) noexcept
{
    if (!g_ready)
        return 0;

    u32 processed = 0;
    while (processed < max_commands)
    {
        u32 header = 0;
        if (!fifo_peek(header))
            break;

        // Header layout:
        //   bits 0..13  : method index  (byte offset / 4)
        //   bits 14..15 : subchannel    (ignored in 6A)
        //   bits 16..17 : count - 1     (0 => 1 data word)
        //   bits 18..31 : reserved (must be 0)
        const u32 method_idx = header & 0x3FFFu;
        const u32 count      = ((header >> 16) & 0x3u) + 1u;

        // Need header + `count` data words.
        if (fifo_used() < (count + 1u))
            break;

        (void)fifo_pop(header); // consume the header we just peeked

        u32 data[4] = {0, 0, 0, 0};
        for (u32 i = 0; i < count; ++i)
        {
            u32 word = 0;
            if (!fifo_pop(word))
                break;
            data[i] = word;
        }

        const u32 byte_off = method_idx << 2;

        // Fall-through state register file: every method writes its
        // data words into g_regs starting at method_idx. This mirrors
        // the hardware's behaviour of auto-incrementing the register
        // pointer across the data words of one command.
        for (u32 i = 0; i < count; ++i)
        {
            const u32 idx = method_idx + i;
            if (idx < kRegisterCount)
                g_regs[idx] = data[i];
        }

        Handler h = handler_for(byte_off);
        if (h)
            h(count, data);
        else
            handle_unknown(byte_off);

        ++processed;
        ++g_commands;
    }
    return processed;
}

u32 Rsx::read_reg(u32 byte_offset) noexcept
{
    const u32 idx = byte_offset >> 2;
    return (idx < kRegisterCount) ? g_regs[idx] : 0u;
}

u64 Rsx::commands()  noexcept { return g_commands; }
u64 Rsx::draws()     noexcept { return g_draws; }
u64 Rsx::presents()  noexcept { return g_presents; }
u64 Rsx::clears()    noexcept { return g_clears; }
u64 Rsx::unknowns()  noexcept { return g_unknowns; }
u64 Rsx::fifo_depth() noexcept { return static_cast<u64>(fifo_used()); }

const Surface& Rsx::target() noexcept { return g_target; }

} // namespace notyvos::ps3::rsx