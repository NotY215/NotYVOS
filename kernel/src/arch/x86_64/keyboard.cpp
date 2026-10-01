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
bool g_alt = false;
volatile u8 g_buf[kBufSize];
volatile u32 g_head = 0;
volatile u32 g_tail = 0;
u64 g_key_count = 0;

// Set on the first keyboard_init() call. On ACPI restart this stays true
// because the kernel reloads from scratch — but the i8042 hardware
// survives the reset, so we must NOT touch it again.
bool g_kbd_hardware_initialized = false;

// Alt-Tab signals.
bool g_alt_tab_pending = false;
bool g_alt_release_pending = false;

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

// Called on the second and subsequent boots. Does not touch the
// controller self-test or config byte — those are preserved through an
// ACPI restart, and re-running them corrupts the mouse clock on VBox.
bool keyboard_reinit_impl() noexcept
{
    flush_output();

    // Re-enable both device ports. Safe no-ops if already enabled.
    write_cmd(0xAE); // enable kbd
    write_cmd(0xA8); // enable aux

    // Clear software state; the hardware state is already correct.
    g_shift = false;
    g_ctrl = false;
    g_extended = false;
    g_alt = false;
    g_head = 0;
    g_tail = 0;

    log::write(log::Level::Info, "kbd", "warm re-init (ports re-enabled)");
    return true;
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

    // Alt handling — intercept before anything else so Alt+Tab does not
    // produce a literal Tab into the shell.
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

    if (g_alt && sc == 0x0F) // Tab while Alt held
    {
        g_alt_tab_pending = true;
        return;
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
    // Warm boot path: hardware is already live, do not re-init.
    if (g_kbd_hardware_initialized)
        return keyboard_reinit_impl();

    log::write(log::Level::Info, "kbd", "init start");

    // Full i8042 reset (cold boot only).
    write_cmd(0xAD); // disable kbd
    write_cmd(0xA7); // disable aux
    flush_output();

    write_cmd(0xAA); // self-test -> 0x55
    u8 selftest = 0;
    if (wait_output_full(500000))
        selftest = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "i8042 self-test -> 0x%llx",
               static_cast<unsigned long long>(selftest));

    write_cmd(0x20); // read config
    u8 cfg = 0;
    if (wait_output_full(500000))
        cfg = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "firmware cfg = 0x%llx",
               static_cast<unsigned long long>(cfg));

    // Force the config to a known-good state. Always write, even if it
    // matches, so the mouse clock and both interrupts are guaranteed on
    // after a warm reset.
    u8 target = cfg;
    target |= 0x01;                   // kbd IRQ on IRQ1
    target |= 0x02;                   // mouse IRQ on IRQ12
    target |= 0x40;                   // translate scancode set 2 to set 1
    target &= ~static_cast<u8>(0x10); // enable kbd clock
    target &= ~static_cast<u8>(0x20); // enable mouse clock

    write_cmd(0x60);
    write_data(target);
    write_cmd(0x20);
    u8 readback = 0;
    if (wait_output_full(500000))
        readback = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "wrote cfg 0x%llx, readback 0x%llx",
               static_cast<unsigned long long>(target), static_cast<unsigned long long>(readback));

    write_cmd(0xAE); // enable kbd port
    write_cmd(0xA8); // enable aux port

    flush_output();
    write_data(0xF6); // set defaults
    u8 ack = 0;
    if (wait_output_full(500000))
        ack = inb(kDataPort);
    (void)ack;

    write_data(0xF4); // enable scanning
    ack = 0;
    if (wait_output_full(500000))
        ack = inb(kDataPort);
    log::write(log::Level::Info, "kbd", "0xF4 -> 0x%llx", static_cast<unsigned long long>(ack));

    log::write(log::Level::Info, "kbd", "i8042 ready");
    g_kbd_hardware_initialized = true;
    return true;
}

bool keyboard_reinit() noexcept
{
    return keyboard_reinit_impl();
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
