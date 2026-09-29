#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

constexpr u32 kMaxWindows = 8;
constexpr u32 kWinTitleMax = 24;

enum class WindowKind : u8
{
    Terminal,
    About,
    Generic
};

struct Window
{
    i32 x;
    i32 y;
    i32 w;
    i32 h;
    bool visible;
    bool focused;
    WindowKind kind;
    char title[kWinTitleMax];
};

class Compositor
{
public:
    static void init() noexcept;
    static bool ready() noexcept;

    // Called from the boot task's idle loop. Never from an IRQ.
    static void tick() noexcept;

    static void invalidate() noexcept;

    static void term_put(char c) noexcept;
    static void term_clear() noexcept;
    static u32 term_cols() noexcept;
    static u32 term_rows() noexcept;

    static void update_clock(u64 seconds) noexcept;
};

} // namespace notyvos::gfx
