#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{

constexpr u16 kDataPort = 0x60;
constexpr u16 kStatusPort = 0x64;
constexpr u16 kCmdPort = 0x64;
constexpr usize kBufSize = 256;

const char kMap[128] = {0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
                        '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
                        '\n', 0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
                        0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
                        0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0};
const char kMapShift[128] = {0,    27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
                             '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
                             '\n', 0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
                             0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*',
                             0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0};

bool g_shift = false;
volatile u8 g_buf[kBufSize];
volatile u32 g_head = 0;
volatile u32 g_tail = 0;
u64 g_total_scancodes = 0;
u64 g_raw_bytes_seen = 0;

inline void push_char(char c)
{
    const u32 next = (g_head + 1) % kBufSize;
    if (next == g_tail)
        return;
    g_buf[g_head] = static_cast<u8>(c);
    g_head = next;
}

bool wait_input_clear(u32 timeout_iter = 1000000) noexcept
{
    for (u32 i = 0; i < timeout_iter; ++i)
    {
        if ((inb(kStatusPort) & 0x02) == 0)
            return true;
        io_wait();
    }
    return false;
}

bool wait_output_full(u32 timeout_iter = 1000000) noexcept
{
    for (u32 i = 0; i < timeout_iter; ++i)
    {
        if (inb(kStatusPort) & 0x01)
            return true;
        io_wait();
    }
    return false;
}

void write_cmd(u8 cmd) noexcept
{
    if (wait_input_clear())
        outb(kCmdPort, cmd);
}
void write_data(u8 data) noexcept
{
    if (wait_input_clear())
        outb(kDataPort, data);
}

void flush_output() noexcept
{
    for (int i = 0; i < 1000; ++i)
    {
        if ((inb(kStatusPort) & 0x01) == 0)
            break;
        (void)inb(kDataPort);
    }
}

void process_scancode(u8 sc)
{
    ++g_raw_bytes_seen;
    if (g_raw_bytes_seen <= 3)
    {
        log::write(log::Level::Warn, "kbd", "raw byte #%llu: 0x%llx",
                   static_cast<unsigned long long>(g_raw_bytes_seen),
                   static_cast<unsigned long long>(sc));
    }

    if (sc == 0xFA || sc == 0xE0 || sc == 0xE1)
        return;
    if (sc & 0x80)
    {
        const u8 code = static_cast<u8>(sc & 0x7F);
        if (code == 0x2A || code == 0x36)
            g_shift = false;
        return;
    }
    if (sc == 0x2A || sc == 0x36)
    {
        g_shift = true;
        return;
    }
    if (sc >= 128)
        return;
    const char c = g_shift ? kMapShift[sc] : kMap[sc];
    if (c == 0)
        return;

    if (g_total_scancodes < 8)
    {
        log::write(log::Level::Warn, "kbd", "key #%llu: 0x%llx -> '%c'",
                   static_cast<unsigned long long>(g_total_scancodes),
                   static_cast<unsigned long long>(sc), c);
    }
    ++g_total_scancodes;
    push_char(c);
}

} // namespace

bool keyboard_init() noexcept
{
    log::write(log::Level::Info, "kbd", "starting minimal PS/2 init");

    // Drain anything VirtualBox left in the buffer.
    flush_output();

    // Read the config byte that VirtualBox set up. Do not modify the
    // IRQ and clock bits unless the IRQ is off.
    write_cmd(0x20);
    u8 cfg = 0;
    if (!wait_output_full(500000))
    {
        log::write(log::Level::Error, "kbd", "cfg read failed");
        return false;
    }
    cfg = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "initial cfg = 0x%llx",
               static_cast<unsigned long long>(cfg));

    bool need_write = false;

    // Enable IRQ1 if it is off.
    if ((cfg & 0x01) == 0)
    {
        cfg |= 0x01;
        need_write = true;
    }
    // Enable translation if it is off (VirtualBox sends set 2 by default).
    if ((cfg & 0x40) == 0)
    {
        cfg |= 0x40;
        need_write = true;
    }
    // Enable keyboard clock if it is off.
    if (cfg & 0x10)
    {
        cfg &= ~static_cast<u8>(0x10);
        need_write = true;
    }

    if (need_write)
    {
        write_cmd(0x60);
        write_data(cfg);
        write_cmd(0x20);
        u8 rb = 0;
        if (wait_output_full(500000))
            rb = inb(kDataPort);
        log::write(log::Level::Info, "kbd", "wrote cfg 0x%llx, readback 0x%llx",
                   static_cast<unsigned long long>(cfg), static_cast<unsigned long long>(rb));
    }
    else
    {
        log::write(log::Level::Info, "kbd", "cfg already correct, no write needed");
    }

    // Send 0xF4 (Enable Scanning). Expect 0xFA ACK.
    write_data(0xF4);
    if (wait_output_full(500000))
    {
        const u8 ack = inb(kDataPort);
        log::write(log::Level::Info, "kbd", "0xF4 ack = 0x%llx",
                   static_cast<unsigned long long>(ack));
    }
    else
    {
        log::write(log::Level::Warn, "kbd", "0xF4 timeout");
    }

    // Final status read for the log.
    const u8 st = inb(kStatusPort);
    log::write(log::Level::Info, "kbd", "PS/2 init done. status=0x%llx",
               static_cast<unsigned long long>(st));

    return true;
}

void keyboard_irq_handler() noexcept
{
    while (inb(kStatusPort) & 0x01)
    {
        const u8 sc = inb(kDataPort);
        process_scancode(sc);
    }
}

bool keyboard_poll() noexcept
{
    if ((inb(kStatusPort) & 0x01) == 0)
        return false;
    const u8 sc = inb(kDataPort);
    process_scancode(sc);
    return true;
}

i32 keyboard_pop() noexcept
{
    if (g_head == g_tail)
        return -1;
    const u8 c = g_buf[g_tail];
    g_tail = (g_tail + 1) % kBufSize;
    return static_cast<i32>(c);
}

bool keyboard_has_data() noexcept
{
    return g_head != g_tail;
}
u64 keyboard_irq_count() noexcept
{
    return g_total_scancodes;
}
void keyboard_inject(char c) noexcept
{
    push_char(c);
}

} // namespace notyvos::arch::x86_64
