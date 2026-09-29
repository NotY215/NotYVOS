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
u8 g_packet[3] = {};
i32 g_x = 512;
i32 g_y = 384;
bool g_left = false, g_right = false, g_middle = false;
u64 g_packet_count = 0;
u64 g_irq_count = 0;

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

void mouse_write(u8 v) noexcept
{
    write_cmd(0xD4);
    write_data(v);
    (void)read_data();
}

} // namespace

bool mouse_init() noexcept
{
    log::write(log::Level::Info, "mouse", "init");

    // Enable the aux port.
    write_cmd(0xA8);

    // Read-modify-write config: set IRQ12 (bit 1), clear mouse clock
    // disable (bit 5).
    write_cmd(0x20);
    u8 cfg = read_data();
    u8 target = cfg;
    target |= 0x02;
    target &= ~static_cast<u8>(0x20);

    if (target != cfg)
    {
        write_cmd(0x60);
        write_data(target);
        write_cmd(0x20);
        u8 rb = read_data();
        log::write(log::Level::Info, "mouse", "cfg 0x%llx -> 0x%llx (readback 0x%llx)",
                   static_cast<unsigned long long>(cfg), static_cast<unsigned long long>(target),
                   static_cast<unsigned long long>(rb));
    }
    else
    {
        log::write(log::Level::Info, "mouse", "cfg unchanged (0x%llx)",
                   static_cast<unsigned long long>(cfg));
    }

    // Mouse: set defaults, then enable reporting.
    mouse_write(0xF6);
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
    ++g_irq_count;
    while (inb(kStatusPort) & 0x01)
    {
        const u8 st = inb(kStatusPort);
        const u8 byte = inb(kDataPort);

        if ((st & 0x20) == 0)
            continue; // keyboard byte

        if (g_cycle == 0 && (byte & 0x08) == 0)
            continue;

        g_packet[g_cycle++] = byte;
        if (g_cycle < 3)
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
            log::write(log::Level::Warn, "mouse",
                       "packet #%llu flags=0x%llx dx=%d dy=%d at (%d,%d)",
                       static_cast<unsigned long long>(g_packet_count),
                       static_cast<unsigned long long>(flags), static_cast<i64>(dx),
                       static_cast<i64>(dy), static_cast<i64>(g_x), static_cast<i64>(g_y));
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
