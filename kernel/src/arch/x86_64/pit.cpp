#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
constexpr u16 kPitChannel0 = 0x40;
constexpr u16 kPitCommand = 0x43;
constexpr u32 kPitBaseFreq = 1193182u;
volatile u64 g_ticks = 0;
u32 g_frequency = 0;
} // namespace

void pit_init(u32 hz) noexcept
{
    g_frequency = hz;
    const u32 divisor = kPitBaseFreq / hz;

    outb(kPitCommand, 0x36); // channel 0, mode 3, binary, lo/hi
    outb(kPitChannel0, static_cast<u8>(divisor & 0xFF));
    outb(kPitChannel0, static_cast<u8>((divisor >> 8) & 0xFF));

    log::write(log::Level::Info, "pit", "programmed %u Hz (divisor %u)", static_cast<u64>(hz),
               static_cast<u64>(divisor));
}

u64 pit_ticks() noexcept
{
    return g_ticks;
}

void pit_on_tick() noexcept
{
    // C++20 deprecates ++/--/compound assignment on volatile objects.
    // Use explicit read-modify-write.
    g_ticks = g_ticks + 1;

    if (g_frequency != 0 && (g_ticks % g_frequency) == 0)
    {
        log::write(log::Level::Debug, "pit", "tick %u", static_cast<u64>(g_ticks));
    }
}

} // namespace notyvos::arch::x86_64
