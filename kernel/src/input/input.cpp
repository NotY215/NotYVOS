#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/input/input.hpp>

namespace notyvos::input
{

namespace
{

struct MouseState
{
    i32  x;
    i32  y;
    i32  wheel;
    bool left;
    bool right;
    bool middle;
    u64  total;
    u64  from_ps2;
    u64  from_usb;
    u64  from_synth;
};

MouseState g_mouse = { 0, 0, 0, false, false, false, 0, 0, 0, 0 };

inline void tally_mouse(Source s) noexcept
{
    ++g_mouse.total;
    switch (s)
    {
    case Source::Ps2:       ++g_mouse.from_ps2;   break;
    case Source::Usb:       ++g_mouse.from_usb;   break;
    case Source::Synthetic: ++g_mouse.from_synth; break;
    }
}

u64 g_kb_total = 0;
u64 g_kb_ps2   = 0;
u64 g_kb_usb   = 0;

} // namespace

namespace mouse
{

void add_delta(Source src, i32 dx, i32 dy) noexcept
{
    g_mouse.x += dx;
    g_mouse.y += dy;
    tally_mouse(src);
    clamp_to_screen();
}

void add_wheel(Source src, i32 delta) noexcept
{
    g_mouse.wheel += delta;
    tally_mouse(src);
}

void set_button(Source src, Button b, bool pressed) noexcept
{
    switch (b)
    {
    case Button::Left:   g_mouse.left   = pressed; break;
    case Button::Right:  g_mouse.right  = pressed; break;
    case Button::Middle: g_mouse.middle = pressed; break;
    }
    tally_mouse(src);
}

void set_position(Source src, i32 x, i32 y) noexcept
{
    g_mouse.x = x;
    g_mouse.y = y;
    tally_mouse(src);
    clamp_to_screen();
}

i32  x()      noexcept { return g_mouse.x; }
i32  y()      noexcept { return g_mouse.y; }
i32  wheel()  noexcept { return g_mouse.wheel; }
void wheel_clear() noexcept { g_mouse.wheel = 0; }
bool left()   noexcept { return g_mouse.left; }
bool right()  noexcept { return g_mouse.right; }
bool middle() noexcept { return g_mouse.middle; }

void clamp_to_screen() noexcept
{
    if (!fb::Framebuffer::ready())
        return;
    const i32 w = static_cast<i32>(fb::Framebuffer::width());
    const i32 h = static_cast<i32>(fb::Framebuffer::height());
    if (w <= 0 || h <= 0)
        return;
    if (g_mouse.x < 0)   g_mouse.x = 0;
    if (g_mouse.y < 0)   g_mouse.y = 0;
    if (g_mouse.x > w - 1) g_mouse.x = w - 1;
    if (g_mouse.y > h - 1) g_mouse.y = h - 1;
}

u64 events()     noexcept { return g_mouse.total; }
u64 ps2_events() noexcept { return g_mouse.from_ps2; }
u64 usb_events() noexcept { return g_mouse.from_usb; }

} // namespace mouse

namespace keyboard
{

void push(Source src, char c) noexcept
{
    // The ring buffer lives in arch/x86_64/keyboard.cpp; both drivers and
    // the serial console reach it through this one function so that stats
    // are accurate.
    arch::x86_64::keyboard_inject(c);

    ++g_kb_total;
    switch (src)
    {
    case Source::Ps2: ++g_kb_ps2; break;
    case Source::Usb: ++g_kb_usb; break;
    default: break;
    }
}

i32  pop()      noexcept { return arch::x86_64::keyboard_pop(); }
bool has_data() noexcept { return arch::x86_64::keyboard_has_data(); }

u64 events()     noexcept { return g_kb_total; }
u64 ps2_events() noexcept { return g_kb_ps2; }
u64 usb_events() noexcept { return g_kb_usb; }

} // namespace keyboard

} // namespace notyvos::input