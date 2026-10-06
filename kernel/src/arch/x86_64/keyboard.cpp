#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/input/input.hpp>
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
bool g_alt = false;
bool g_extended = false;
volatile u8 g_buf[kBufSize];
volatile u32 g_head = 0;
volatile u32 g_tail = 0;
u64 g_key_count = 0;

bool g_alt_tab_pending = false;
bool g_alt_release_pending = false;
bool g_alt_f4_pending = false;
bool g_ctrl_c_pending = false;
bool g_ctrl_x_pending = false;
bool g_ctrl_v_pending = false;

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

void process_scancode(u8 sc)
{
    if (sc == 0xFA)
        return;

    if (sc == 0xE0)
    {
        g_extended = true;
        return;
    }

    if (g_extended)
    {
        g_extended = false;
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

    // Alt press / release.
    if (sc == 0x38)
    {
        g_alt = true;
        return;
    }
    if (sc == 0xB8)
    {
        g_alt = false;
        g_alt_release_pending = true;
        return;
    }

    if (g_alt && sc == 0x0F)
    {
        g_alt_tab_pending = true;
        return;
    } // Alt+Tab
    if (g_alt && sc == 0x3E)
    {
        g_alt_f4_pending = true;
        return;
    } // Alt+F4

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

    // Ctrl shortcuts are latched; the compositor routes them by focus.
    if (g_ctrl && (c == 'c' || c == 'C'))
    {
        g_ctrl_c_pending = true;
        return;
    }
    if (g_ctrl && (c == 'x' || c == 'X'))
    {
        g_ctrl_x_pending = true;
        return;
    }
    if (g_ctrl && (c == 'v' || c == 'V'))
    {
        g_ctrl_v_pending = true;
        return;
    }

        if (g_key_count < 3)
    {
        log::write(log::Level::Debug, "kbd", "key #%llu sc=0x%llx -> '%c'",
                   static_cast<unsigned long long>(g_key_count),
                   static_cast<unsigned long long>(sc), c);
    }
    ++g_key_count;
    input::keyboard::push(input::Source::Ps2, c);
}

} // namespace

// Read the config byte. Returns false on timeout.
bool read_config(u8& out) noexcept
{
    write_cmd(0x20);
    if (!wait_output_full(300000))
        return false;
    out = inb(kDataPort);
    return true;
}

// Write the config byte.
bool write_config(u8 cfg) noexcept
{
    write_cmd(0x60);
    write_data(cfg);
    return true;
}

bool keyboard_init() noexcept
{
    log::write(log::Level::Info, "kbd", "init start");

    write_cmd(0xAD);
    write_cmd(0xA7);
    flush_output();

    u8 selftest = 0;
    bool st_ok = false;
    for (u32 attempt = 0; attempt < 3 && !st_ok; ++attempt)
    {
        write_cmd(0xAA);
        if (wait_output_full(400000))
        {
            selftest = inb(kDataPort);
            if (selftest == 0x55u)
                st_ok = true;
        }
    }
    log::write(log::Level::Info, "kbd", "i8042 self-test -> 0x%llx (%s)",
               static_cast<unsigned long long>(selftest), st_ok ? "ok" : "timeout");

    u8 cfg = 0x47;
    u8 readback = 0;
    if (read_config(readback))
        cfg = readback;
    log::write(log::Level::Info, "kbd", "firmware cfg = 0x%llx",
               static_cast<unsigned long long>(cfg));

    u8 target = cfg;
    target |= 0x01;
    target |= 0x02;
    target |= 0x40;
    target &= ~static_cast<u8>(0x10);
    target &= ~static_cast<u8>(0x20);

    write_config(target);
    readback = 0;
    if (read_config(readback))
    {
        log::write(log::Level::Info, "kbd", "wrote cfg 0x%llx, readback 0x%llx",
                   static_cast<unsigned long long>(target),
                   static_cast<unsigned long long>(readback));
    }

    write_cmd(0xAE);
    write_cmd(0xA8);

    flush_output();
    write_data(0xF6);
    (void)wait_output_full(300000);
    (void)inb(kDataPort);

    write_data(0xF4);
    u8 ack = 0;
    if (wait_output_full(300000))
        ack = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "0xF4 -> 0x%llx", static_cast<unsigned long long>(ack));

    log::write(log::Level::Info, "kbd", "i8042 ready");
    return true;
}

bool keyboard_reinit() noexcept
{
    return keyboard_init();
}

AltTabEvent keyboard_alt_tab_event() noexcept
{
    if (g_alt_tab_pending)
    {
        g_alt_tab_pending = false;
        return AltTabEvent::Cycle;
    }
    if (g_alt_release_pending)
    {
        g_alt_release_pending = false;
        return AltTabEvent::Commit;
    }
    return AltTabEvent::None;
}

bool keyboard_alt_f4_event() noexcept
{
    if (g_alt_f4_pending)
    {
        g_alt_f4_pending = false;
        return true;
    }
    return false;
}

bool keyboard_ctrl_c_event() noexcept
{
    if (g_ctrl_c_pending)
    {
        g_ctrl_c_pending = false;
        return true;
    }
    return false;
}

bool keyboard_ctrl_x_event() noexcept
{
    if (g_ctrl_x_pending)
    {
        g_ctrl_x_pending = false;
        return true;
    }
    return false;
}

bool keyboard_ctrl_v_event() noexcept
{
    if (g_ctrl_v_pending)
    {
        g_ctrl_v_pending = false;
        return true;
    }
    return false;
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

// Inject a character directly. Used by the serial console fallback and by
// the unified input layer's synthetic path.
void keyboard_inject(char c) noexcept
{
    push_char(c);
}

} // namespace notyvos::arch::x86_64
