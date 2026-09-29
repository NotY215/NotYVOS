#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/log.hpp>
#include <kernel/sched/scheduler.hpp>

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
bool g_ctrl = false;
bool g_extended = false;
volatile u8 g_buf[kBufSize];
volatile u32 g_head = 0;
volatile u32 g_tail = 0;
u64 g_key_count = 0;

inline void push_char(char c)
{
    const u32 next = (g_head + 1) % kBufSize;
    if (next == g_tail)
        return;
    g_buf[g_head] = static_cast<u8>(c);
    g_head = next;
}

bool wait_input_clear(u32 timeout = 1000000) noexcept
{
    for (u32 i = 0; i < timeout; ++i)
    {
        if ((inb(kStatusPort) & 0x02) == 0)
            return true;
        io_wait();
    }
    return false;
}

bool wait_output_full(u32 timeout = 1000000) noexcept
{
    for (u32 i = 0; i < timeout; ++i)
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

void mouse_write(u8 byte) noexcept
{
    write_cmd(0xD4);
    write_data(byte);
    if (wait_output_full(500000))
        (void)inb(kDataPort);
}

void process_scancode(u8 sc)
{
    // Discard ACKs, extended prefixes are handled below.
    if (sc == 0xFA)
        return;

    // Extended prefix (E0) — the next byte is a special key.
    if (sc == 0xE0)
    {
        g_extended = true;
        return;
    }

    if (g_extended)
    {
        g_extended = false;
        // Make/break both go through here; break has bit 7 set.
        const bool release = (sc & 0x80) != 0;
        if (release)
            return;
        switch (sc)
        {
        case 0x48:
            push_char(kKeyUp);
            return;
        case 0x50:
            push_char(kKeyDown);
            return;
        case 0x4B:
            push_char(kKeyLeft);
            return;
        case 0x4D:
            push_char(kKeyRight);
            return;
        case 0x47:
            push_char(kKeyHome);
            return;
        case 0x4F:
            push_char(kKeyEnd);
            return;
        case 0x49:
            push_char(kKeyPgUp);
            return;
        case 0x51:
            push_char(kKeyPgDn);
            return;
        default:
            return;
        }
    }

    if (sc == 0x1D)
    {
        g_ctrl = true;
        return;
    }
    if (sc == 0x9D)
    {
        g_ctrl = false;
        return;
    }

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

    if (g_ctrl && (c == 'c' || c == 'C'))
    {
        log::write(log::Level::Info, "kbd", "Ctrl+C");
        sched::scheduler_deliver_sigint();
        return;
    }

    if (g_key_count < 20)
    {
        log::write(log::Level::Warn, "kbd", "key #%llu sc=0x%llx -> '%c'",
                   static_cast<unsigned long long>(g_key_count),
                   static_cast<unsigned long long>(sc), c);
    }
    ++g_key_count;
    push_char(c);
}

} // namespace

bool keyboard_init() noexcept
{
    log::write(log::Level::Info, "kbd", "init start");

    flush_output();

    write_cmd(0x20);
    u8 cfg = 0;
    if (wait_output_full(500000))
        cfg = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "firmware cfg = 0x%llx",
               static_cast<unsigned long long>(cfg));

    u8 target = cfg;
    target |= 0x01;
    target |= 0x02;
    target |= 0x40;
    target &= ~static_cast<u8>(0x10);

    if (target != cfg)
    {
        write_cmd(0x60);
        write_data(target);
        write_cmd(0x20);
        u8 rb = 0;
        if (wait_output_full(500000))
            rb = inb(kDataPort);
        log::write(log::Level::Info, "kbd", "wrote cfg 0x%llx, readback 0x%llx",
                   static_cast<unsigned long long>(target), static_cast<unsigned long long>(rb));
    }
    else
    {
        log::write(log::Level::Info, "kbd", "cfg unchanged");
    }

    write_data(0xF4);
    u8 ack = 0;
    if (wait_output_full(500000))
        ack = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "0xF4 -> 0x%llx", static_cast<unsigned long long>(ack));

    write_cmd(0xA8);
    log::write(log::Level::Info, "kbd", "aux port enabled");

    mouse_write(0xF6);
    mouse_write(0xF4);
    log::write(log::Level::Info, "kbd", "mouse init sent");

    return true;
}

void keyboard_irq_handler() noexcept
{
    while (inb(kStatusPort) & 0x01)
    {
        const u8 st = inb(kStatusPort);
        const u8 byte = inb(kDataPort);
        if (st & 0x20)
            continue;
        process_scancode(byte);
    }
}

bool keyboard_poll() noexcept
{
    if ((inb(kStatusPort) & 0x01) == 0)
        return false;
    const u8 st = inb(kStatusPort);
    const u8 byte = inb(kDataPort);
    if (st & 0x20)
        return false;
    process_scancode(byte);
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
    return g_key_count;
}
void keyboard_inject(char c) noexcept
{
    push_char(c);
}

} // namespace notyvos::arch::x86_64
