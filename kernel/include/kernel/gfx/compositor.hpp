#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

constexpr u32 kMaxWindows = 8;
constexpr u32 kWinTitleMax = 24;

enum class WindowKind : u8
{
    Terminal,
    Explorer,
    Settings,
    Bin,
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
    bool minimized;
    bool maximized;
    i32 saved_x, saved_y, saved_w, saved_h;
    WindowKind kind;
    char title[kWinTitleMax];
};

class Compositor
{
public:
    static void init() noexcept;
    static bool ready() noexcept;
    static void tick() noexcept;
    static void invalidate() noexcept;

    static void term_put(char c) noexcept;
    static void term_clear() noexcept;
    static u32 term_cols() noexcept;
    static u32 term_rows() noexcept;

    static void term_scroll_by(i32 delta) noexcept;
    static void term_scroll_bottom() noexcept;

    static void update_clock() noexcept;

    // Power actions. Called from the Start menu.
    static void machine_shutdown() noexcept;
    static void machine_restart() noexcept;
};

} // namespace notyvos::gfx
