#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/arch/x86_64/pit.hpp>
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
u64 g_irq_count = 0;
u64 g_first_key_scancode = 0;

inline void push_char(char c)
{
    const u32 next = (g_head + 1) % kBufSize;
    if (next == g_tail)
        return;
    g_buf[g_head] = static_cast<u8>(c);
    g_head = next;
}

void process_scancode(u8 sc)
{
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

    if (g_first_key_scancode == 0)
    {
        g_first_key_scancode = sc;
        log::write(log::Level::Info, "kbd", "first key received (scancode=0x%llx)",
                   static_cast<unsigned long long>(sc));
    }
    push_char(c);
}

} // namespace

void keyboard_init() noexcept
{
    for (int i = 0; i < 1000 && (inb(kStatusPort) & 0x01); ++i)
        (void)inb(kDataPort);

    outb(kCmdPort, 0x20);
    for (int i = 0; i < 100000 && !(inb(kStatusPort) & 0x01); ++i)
    {
    }
    u8 cfg = (inb(kStatusPort) & 0x01) ? inb(kDataPort) : 0x00;

    cfg |= 0x01;                   // enable IRQ1
    cfg |= 0x40;                   // translate to set 1
    cfg &= ~static_cast<u8>(0x10); // enable keyboard clock

    outb(kCmdPort, 0x60);
    outb(kDataPort, cfg);

    outb(kDataPort, 0xF4); // enable scanning
    for (int i = 0; i < 100000; ++i)
    {
        if (inb(kStatusPort) & 0x01)
        {
            (void)inb(kDataPort);
            break;
        }
    }

    log::write(log::Level::Info, "kbd", "ps/2 keyboard initialized (cfg=0x%llx)",
               static_cast<unsigned long long>(cfg));
}

void keyboard_irq_handler() noexcept
{
    bool any = false;
    while (inb(kStatusPort) & 0x01)
    {
        const u8 sc = inb(kDataPort);
        process_scancode(sc);
        any = true;
    }
    if (any)
        ++g_irq_count;
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
    return g_irq_count;
}

} // namespace notyvos::arch::x86_64
