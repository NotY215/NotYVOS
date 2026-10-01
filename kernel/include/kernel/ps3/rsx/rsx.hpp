#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::rsx
{

// ===========================================================================
// RSX compatibility layer — Phase 6A (structural)
//
// Real RSX hardware is an NV40-family GPU. Games drive it by writing
// commands into a FIFO in main memory; the RSX reads them and updates its
// state registers, then rasterizes when it sees a draw method.
//
// Phase 6A implements the *structural* half of that:
//   - a FIFO ring buffer (words)
//   - method header parsing (14-bit method index, 1..4 data words)
//   - method dispatch to a state register file
//   - control flow: jump, call, return
//   - surfaces (colour + depth targets, pitch, format)
//   - trigger methods: clear, draw, present
//   - unknown-method logging with rate limiting
//
// Phase 6B will add: real vertex assembly, shader binding, rasterization
// through the existing Graphics HAL, and RSX -> NOTYVOS swapchain.
// ===========================================================================

// RSX method space is 16 KB. Real RSX methods are byte-aligned, and the
// FIFO header stores the method index (byte offset / 4) in 14 bits.
constexpr u32 kRegisterSpace  = 0x4000;
constexpr u32 kRegisterCount  = kRegisterSpace / 4;

// Named methods we dispatch on. Values are byte offsets into the space.
namespace method
{
constexpr u32 kNop         = 0x0100;
constexpr u32 kObject      = 0x0180;
constexpr u32 kFifoJump    = 0x0130;
constexpr u32 kFifoCall    = 0x0134;
constexpr u32 kFifoReturn  = 0x0138;
constexpr u32 kSurfaceFmt  = 0x0200;
constexpr u32 kSurfaceCol  = 0x0204; // 64-bit physical address
constexpr u32 kSurfacePit  = 0x0208;
constexpr u32 kSurfaceW    = 0x020C;
constexpr u32 kSurfaceH    = 0x0210;
constexpr u32 kClearColor  = 0x0214; // 4 words (A R G B)
constexpr u32 kClearDepth  = 0x0218;
constexpr u32 kClear       = 0x0220; // trigger
constexpr u32 kDraw        = 0x0224; // trigger
constexpr u32 kPresent     = 0x0228; // trigger
} // namespace method

enum class SurfaceFormat : u32
{
    Unknown   = 0,
    X8R8G8B8  = 1,
    A8R8G8B8  = 2,
    R5G6B5    = 3,
};

struct Surface
{
    u64           address;   // physical byte address in the guest's VRAM
    u32           pitch;     // bytes per row
    u32           width;
    u32           height;
    SurfaceFormat format;
    bool          valid;
};

// FIFO ring buffer. We model it as a fixed-size array of 32-bit words
// with a producer index (put) and a consumer index (get). Real RSX uses
// a shared-memory control block updated via MMIO; that plumbing lands in
// Phase 6B alongside RSX MMIO mapping.
constexpr u32 kFifoWords   = 4096;
constexpr u32 kCallDepth   = 8;

struct Fifo
{
    u32  buffer[kFifoWords];
    u32  put;                 // producer index (word units)
    u32  get;                 // consumer index (word units)
    u32  call_stack[kCallDepth];
    u32  call_sp;
    bool jump_pending;        // next get reads from jump_target
    u32  jump_target;
    u64  total_words_in;
    u64  total_words_out;
};

// Top-level RSX state.
class Rsx
{
public:
    static void init() noexcept;
    static bool ready() noexcept;

    // Host-side FIFO push. Guest code (PPU / SPU) calls this via the
    // RSX MMIO window; in Phase 6A it is a direct API.
    // Returns false if the FIFO is full.
    static bool push(u32 word) noexcept;

    // Process up to `max_commands` complete FIFO commands.
    // Returns the number of commands executed.
    static u32 process(u32 max_commands) noexcept;

    // Read a raw state register (byte offset). Debug accessor.
    static u32 read_reg(u32 byte_offset) noexcept;

    // Stats.
    static u64 commands()   noexcept;
    static u64 draws()      noexcept;
    static u64 presents()   noexcept;
    static u64 clears()     noexcept;
    static u64 unknowns()   noexcept;
    static u64 fifo_depth() noexcept;

    // Surface target the RSX is currently drawing to.
    static const Surface& target() noexcept;
};

} // namespace notyvos::ps3::rsx