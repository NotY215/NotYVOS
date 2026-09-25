#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/log.hpp>
#include <kernel/panic.hpp>

namespace notyvos
{

[[noreturn]] void panic(const char* fmt, ...) noexcept
{
    asm volatile("cli");

    arch::x86_64::SerialPort::write("\n*** NOTYVOS KERNEL PANIC ***\n");

    // Phase 0: forward the format string literally.
    // Phase 1F will add a proper formatter that consumes the varargs.
    log::write(log::Level::Error, "panic", "%s", fmt);

    arch::x86_64::SerialPort::write("System halted.\n");
    halt_forever();
}

} // namespace notyvos
