#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::rsx
{

// ===========================================================================
// RSX compatibility layer.
//
//   6A  structural: FIFO, method dispatch, control flow, surface state
//   6B  rasterizer: points / lines / triangles / strips / fans
//   6C  vertex buffers, indexed draws, depth test, guest memory, interp mode
// ===========================================================================

constexpr u32 kRegisterSpace = 0x4000;
constexpr u32 kRegisterCount = kRegisterSpace / 4;

namespace method
{
constexpr u32 kNop = 0x0100;
constexpr u32 kObject = 0x0180;
constexpr u32 kFifoJump = 0x0130;
constexpr u32 kFifoCall = 0x0134;
constexpr u32 kFifoReturn = 0x0138;

constexpr u32 kSurfaceFmt = 0x0200;
constexpr u32 kSurfaceCol = 0x0204; // 2 words: 64-bit physical address (lo, hi)
constexpr u32 kSurfacePit = 0x0208;
constexpr u32 kSurfaceW = 0x020C;
constexpr u32 kSurfaceH = 0x0210;
constexpr u32 kClearColor = 0x0214;
constexpr u32 kClear = 0x0220;
constexpr u32 kDraw = 0x0224;
constexpr u32 kPresent = 0x0228;

constexpr u32 kPrimType = 0x0300;
constexpr u32 kVertexPush = 0x0304; // 4 words: x, y, z, color
constexpr u32 kVertexFlush = 0x0308;
constexpr u32 kBackground = 0x030C;

// 6C additions.
constexpr u32 kVertexBuffer = 0x0320; // 2 words: 64-bit guest address
constexpr u32 kIndexBuffer = 0x0328;  // 2 words: 64-bit guest address
constexpr u32 kVertexStride = 0x032C; // 1 word: bytes per vertex (min 16)
constexpr u32 kDrawArrays = 0x0330;   // 1 word: vertex count
constexpr u32 kDrawElements = 0x0334; // 1 word: index count
constexpr u32 kDepthEnable = 0x0340;  // 1 word: 0 = off, 1 = on
constexpr u32 kDepthClear = 0x0344;   // 1 word: z clear value
constexpr u32 kInterpMode = 0x0348;   // 1 word: 0 = flat, 1 = smooth
constexpr u32 kScissorX = 0x034C;     // 1 word
constexpr u32 kScissorY = 0x0350;     // 1 word
constexpr u32 kScissorW = 0x0354;     // 1 word
constexpr u32 kScissorH = 0x0358;     // 1 word
} // namespace method

enum class SurfaceFormat : u32
{
    Unknown = 0,
    X8R8G8B8 = 1,
    A8R8G8B8 = 2,
    R5G6B5 = 3,
};

enum class Primitive : u32
{
    Points = 0,
    Lines = 1,
    Triangles = 2,
    TriStrip = 3,
    TriFan = 4,
};

enum class Interp : u32
{
    Flat = 0,
    Smooth = 1,
};

struct Surface
{
    u64 address;
    u32 pitch;
    u32 width;
    u32 height;
    SurfaceFormat format;
    bool valid;
};

struct Vertex
{
    i32 x;
    i32 y;
    u32 z; // smaller = closer
    u32 color;
};

using GuestRead32Fn = bool (*)(void* user, u64 addr, u32* out);

constexpr u32 kFifoWords = 4096;
constexpr u32 kCallDepth = 8;

struct Fifo
{
    u32 buffer[kFifoWords];
    u32 put;
    u32 get;
    u32 call_stack[kCallDepth];
    u32 call_sp;
    bool jump_pending;
    u32 jump_target;
    u64 total_words_in;
    u64 total_words_out;
};

class Rsx
{
public:
    static void init() noexcept;
    static bool ready() noexcept;

    static bool push(u32 word) noexcept;
    static u32 process(u32 max_commands) noexcept;
    static u32 read_reg(u32 byte_offset) noexcept;

    // Raster target in kernel memory.
    static void bind_surface_memory(u32* pixels, u32 width, u32 height, u32 pitch) noexcept;

    // Guest memory read callback (for vertex/index buffers).
    static void set_guest_read32(GuestRead32Fn fn, void* user) noexcept;

    // Stats.
    static u64 commands() noexcept;
    static u64 draws() noexcept;
    static u64 presents() noexcept;
    static u64 clears() noexcept;
    static u64 unknowns() noexcept;
    static u64 fifo_depth() noexcept;
    static u64 primitives_drawn() noexcept;
    static u64 pixels_written() noexcept;

    // Debug: copy pixels into `out`.
    static u32 snapshot(u32* out, u32 max_pixels) noexcept;

    static const Surface& target() noexcept;
};

} // namespace notyvos::ps3::rsx
