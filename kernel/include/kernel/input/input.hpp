#pragma once
#include <kernel/types.hpp>

namespace notyvos::input
{

// Source of an input event. Used for statistics and for future
// per-source routing (e.g. a game may want to ignore the OS cursor).
enum class Source : u8
{
    Ps2       = 0,
    Usb       = 1,
    Synthetic = 2,   // injected by shell, tests, or programmatic control
};

// ---------------------------------------------------------------------------
// Mouse
//
// Single global cursor. Absolute position, relative motion, buttons, wheel.
// Both PS/2 and USB drivers feed this; consumers read from it.
// ---------------------------------------------------------------------------
namespace mouse
{

enum class Button : u8
{
    Left   = 0,
    Right  = 1,
    Middle = 2,
};

// Relative motion. Screen-space, top-left origin, Y grows downward.
void add_delta(Source src, i32 dx, i32 dy) noexcept;

// Wheel ticks. Positive = scroll up.
void add_wheel(Source src, i32 delta) noexcept;

// Button state.
void set_button(Source src, Button b, bool pressed) noexcept;

// Absolute position, clamped to the current framebuffer.
void set_position(Source src, i32 x, i32 y) noexcept;

// Current state.
i32  x()      noexcept;
i32  y()      noexcept;
i32  wheel()  noexcept;      // accumulated, cleared by wheel_clear()
void wheel_clear() noexcept;
bool left()   noexcept;
bool right()  noexcept;
bool middle() noexcept;

// Clamp to the framebuffer. No-op when the framebuffer is not yet ready.
void clamp_to_screen() noexcept;

// Statistics.
u64 events()     noexcept;   // total events from every source
u64 ps2_events() noexcept;
u64 usb_events() noexcept;

} // namespace mouse

// ---------------------------------------------------------------------------
// Keyboard
//
// Thin facade over the existing character ring buffer. Both PS/2 and USB
// push characters here. Consumers pop one byte at a time. Special keys
// (arrows, home/end/pgup/pgdn) are pushed as the kKeyXxx constants from
// arch/x86_64/keyboard.hpp.
// ---------------------------------------------------------------------------
namespace keyboard
{

void push(Source src, char c) noexcept;

// Returns -1 if no character is waiting.
i32  pop()      noexcept;
bool has_data() noexcept;

// Statistics.
u64 events()     noexcept;
u64 ps2_events() noexcept;
u64 usb_events() noexcept;

} // namespace keyboard

} // namespace notyvos::input
