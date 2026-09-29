#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

class Compositor
{
public:
    // Turn the framebuffer into a desktop.
    static void init() noexcept;
    static bool ready() noexcept;

    // Update the on-screen mouse cursor.
    static void tick() noexcept;

    // Terminal window text buffer. Called by Console when in buffered mode.
    static void term_put(char c) noexcept;
    static void term_clear() noexcept;
    static void term_scroll() noexcept;

    // Called once a second by the PIT handler to refresh the taskbar clock.
    static void update_clock(u64 seconds) noexcept;

    // Framebuffer dimensions of the client area, in character cells.
    static u32 term_cols() noexcept;
    static u32 term_rows() noexcept;
};

} // namespace notyvos::gfx
