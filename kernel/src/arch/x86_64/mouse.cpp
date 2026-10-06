#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/input/input.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{

constexpr u16 kDataPort = 0x60;
constexpr u16 kStatusPort = 0x64;
constexpr u16 kCmdPort = 0x64;

u8 g_cycle = 0;
u8 g_packet[4] = {};
u32 g_packet_len = 3;
u64 g_packet_count = 0;
bool g_intellimouse = false;

bool wait_write() noexcept
{
    for (u32 i = 0; i < 1000000u; ++i)
    {
        if ((inb(kStatusPort) & 0x02) == 0)
            return true;
        io_wait();
    }
    return false;
}

bool wait_read() noexcept
{
    for (u32 i = 0; i < 1000000u; ++i)
    {
        if (inb(kStatusPort) & 0x01)
            return true;
        io_wait();
    }
    return false;
}

void write_cmd(u8 c) noexcept
{
    if (wait_write())
        outb(kCmdPort, c);
}
void write_data(u8 d) noexcept
{
    if (wait_write())
        outb(kDataPort, d);
}

u8 read_data() noexcept
{
    if (!wait_read())
        return 0;
    return inb(kDataPort);
}

u8 mouse_write(u8 v) noexcept
{
    write_cmd(0xD4);
    write_data(v);
    return read_data();
}

u8 mouse_get_id() noexcept
{
    (void)mouse_write(0xF2);
    return read_data();
}

void mouse_set_sample(u8 rate) noexcept
{
    (void)mouse_write(0xF3);
    (void)mouse_write(rate);
}

void try_enable_wheel() noexcept
{
    mouse_set_sample(200);
    mouse_set_sample(100);
    mouse_set_sample(80);
    const u8 id = mouse_get_id();
    log::write(log::Level::Info, "mouse", "device id after IntelliMouse magic: 0x%llx",
               static_cast<unsigned long long>(id));
    if (id == 0x03 || id == 0x04)
    {
        g_intellimouse = true;
        g_packet_len = 4;
        log::write(log::Level::Info, "mouse", "wheel enabled (4-byte packets)");
    }
    else
    {
        g_packet_len = 3;
        log::write(log::Level::Info, "mouse", "no wheel (3-byte packets)");
    }
}

} // namespace

bool mouse_init() noexcept
{
    g_cycle = 0;
    for (u32 i = 0; i < 4; ++i)
        g_packet[i] = 0;
    g_intellimouse = false;
    g_packet_len = 3;

    log::write(log::Level::Info, "mouse", "init");

    write_cmd(0xA8);

    write_cmd(0x20);
    u8 cfg = read_data();
    cfg |= 0x02;
    cfg &= ~static_cast<u8>(0x20);
    write_cmd(0x60);
    write_data(cfg);

    (void)mouse_write(0xF6); // set defaults
    try_enable_wheel();      // magic sequence (leaves rate at 80 Hz)

    // Set final sample rate to 200 Hz. The magic sequence leaves the
    // device at 80 Hz, which feels sluggish.
    mouse_set_sample(200);

    (void)mouse_write(0xF4); // enable data reporting

    if (fb::Framebuffer::ready())
    {
        const i32 cx = static_cast<i32>(fb::Framebuffer::width() / 2);
        const i32 cy = static_cast<i32>(fb::Framebuffer::height() / 2);
        input::mouse::set_position(input::Source::Synthetic, cx, cy);
    }

    log::write(log::Level::Info, "mouse", "ready at (%d, %d) @ 200 Hz",
               static_cast<i64>(input::mouse::x()), static_cast<i64>(input::mouse::y()));
    return true;
}

void mouse_irq_handler() noexcept
{
    while (inb(kStatusPort) & 0x01)
    {
        const u8 st = inb(kStatusPort);
        const u8 byte = inb(kDataPort);

        if ((st & 0x20) == 0)
            continue;
        if (g_cycle == 0 && (byte & 0x08) == 0)
            continue;

        g_packet[g_cycle++] = byte;
        if (g_cycle < g_packet_len)
            continue;
        g_cycle = 0;

        const u8 flags = g_packet[0];
        if (flags & 0xC0)
            continue;

        i32 dx = static_cast<i32>(g_packet[1]);
        i32 dy = static_cast<i32>(g_packet[2]);
        if (flags & 0x10)
            dx |= ~0xFF;
        if (flags & 0x20)
            dy |= ~0xFF;

        // PS/2 reports +Y as up; screen-space Y grows downward.
        input::mouse::add_delta(input::Source::Ps2, dx, -dy);

        input::mouse::set_button(input::Source::Ps2, input::mouse::Button::Left,
                                 (flags & 0x01) != 0);
        input::mouse::set_button(input::Source::Ps2, input::mouse::Button::Right,
                                 (flags & 0x02) != 0);
        input::mouse::set_button(input::Source::Ps2, input::mouse::Button::Middle,
                                 (flags & 0x04) != 0);

        if (g_packet_len == 4)
        {
            i8 z = static_cast<i8>(g_packet[3] & 0x0F);
            if (z & 0x08)
                z = static_cast<i8>(z | 0xF0);
            if (z != 0)
                input::mouse::add_wheel(input::Source::Ps2, static_cast<i32>(z));
        }

        if (g_packet_count < 3)
        {
            log::write(log::Level::Debug, "mouse", "packet #%llu flags=0x%llx dx=%d dy=%d wheel=%d",
                       static_cast<unsigned long long>(g_packet_count),
                       static_cast<unsigned long long>(flags), static_cast<i64>(dx),
                       static_cast<i64>(dy), static_cast<i64>(input::mouse::wheel()));
        }
        ++g_packet_count;
    }
}

// Public facade. Preserves the historical API so nothing else has to change.

i32 mouse_x() noexcept
{
    return input::mouse::x();
}
i32 mouse_y() noexcept
{
    return input::mouse::y();
}
i32 mouse_wheel() noexcept
{
    return input::mouse::wheel();
}
void mouse_wheel_clear() noexcept
{
    input::mouse::wheel_clear();
}

bool mouse_left() noexcept
{
    return input::mouse::left();
}
bool mouse_right() noexcept
{
    return input::mouse::right();
}
bool mouse_middle() noexcept
{
    return input::mouse::middle();
}

void mouse_set_position(i32 x, i32 y) noexcept
{
    input::mouse::set_position(input::Source::Synthetic, x, y);
}

} // namespace notyvos::arch::x86_64
