#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/fb/framebuffer.hpp>
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
i32 g_x = 512;
i32 g_y = 384;
i32 g_wheel = 0;
bool g_left = false, g_right = false, g_middle = false;
u64 g_packet_count = 0;
bool g_intellimouse = false;

bool wait_write() noexcept
{
    for (u32 i = 0; i < 1000000; ++i)
    {
        if ((inb(kStatusPort) & 0x02) == 0)
            return true;
        io_wait();
    }
    return false;
}

bool wait_read() noexcept
{
    for (u32 i = 0; i < 1000000; ++i)
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

// Send a byte to the mouse (via 0xD4) and return the ACK.
u8 mouse_write(u8 v) noexcept
{
    write_cmd(0xD4);
    write_data(v);
    return read_data();
}

// Ask the mouse for its device ID.
u8 mouse_get_id() noexcept
{
    mouse_write(0xF2);
    return read_data();
}

// Set the sample rate (used for the IntelliMouse magic sequence).
void mouse_set_sample(u8 rate) noexcept
{
    mouse_write(0xF3);
    mouse_write(rate);
}

void try_enable_wheel() noexcept
{
    // Magic sequence from the IntelliMouse spec.
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
    log::write(log::Level::Info, "mouse", "init");

    write_cmd(0xA8);

    write_cmd(0x20);
    u8 cfg = read_data();
    cfg |= 0x02;
    cfg &= ~static_cast<u8>(0x20);
    write_cmd(0x60);
    write_data(cfg);

    // Set defaults, then try to enable the wheel.
    mouse_write(0xF6);
    try_enable_wheel();
    // Enable data reporting.
    mouse_write(0xF4);

    if (fb::Framebuffer::ready())
    {
        g_x = static_cast<i32>(fb::Framebuffer::width() / 2);
        g_y = static_cast<i32>(fb::Framebuffer::height() / 2);
    }

    log::write(log::Level::Info, "mouse", "ready at (%d, %d)", static_cast<i64>(g_x),
               static_cast<i64>(g_y));
    return true;
}

void mouse_irq_handler() noexcept
{
    while (inb(kStatusPort) & 0x01)
    {
        const u8 st = inb(kStatusPort);
        const u8 byte = inb(kDataPort);

        if ((st & 0x20) == 0)
            continue; // keyboard byte
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

        g_x += dx;
        g_y -= dy;

        g_left = (flags & 0x01) != 0;
        g_right = (flags & 0x02) != 0;
        g_middle = (flags & 0x04) != 0;

        if (g_packet_len == 4)
        {
            // 4th byte is signed Z movement (wheel).
            i8 z = static_cast<i8>(g_packet[3] & 0x0F);
            if (z & 0x08)
                z = static_cast<i8>(z | 0xF0); // sign extend 4-bit
            g_wheel += static_cast<i32>(z);
        }

        if (fb::Framebuffer::ready())
        {
            const i32 W = static_cast<i32>(fb::Framebuffer::width());
            const i32 H = static_cast<i32>(fb::Framebuffer::height());
            if (g_x < 0)
                g_x = 0;
            if (g_y < 0)
                g_y = 0;
            if (g_x > W - 1)
                g_x = W - 1;
            if (g_y > H - 1)
                g_y = H - 1;
        }

        if (g_packet_count < 20)
        {
            log::write(log::Level::Warn, "mouse", "packet #%llu flags=0x%llx dx=%d dy=%d wheel=%d",
                       static_cast<unsigned long long>(g_packet_count),
                       static_cast<unsigned long long>(flags), static_cast<i64>(dx),
                       static_cast<i64>(dy), static_cast<i64>(g_wheel));
        }
        ++g_packet_count;
    }
}

i32 mouse_x() noexcept
{
    return g_x;
}
i32 mouse_y() noexcept
{
    return g_y;
}
i32 mouse_wheel() noexcept
{
    return g_wheel;
}
void mouse_wheel_clear() noexcept
{
    g_wheel = 0;
}

bool mouse_left() noexcept
{
    return g_left;
}
bool mouse_right() noexcept
{
    return g_right;
}
bool mouse_middle() noexcept
{
    return g_middle;
}

void mouse_set_position(i32 x, i32 y) noexcept
{
    g_x = x;
    g_y = y;
}

} // namespace notyvos::arch::x86_64
