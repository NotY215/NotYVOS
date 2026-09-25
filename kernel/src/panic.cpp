#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/log.hpp>
#include <kernel/panic.hpp>
#include <stdarg.h>

namespace notyvos
{

[[noreturn]] void panic(const char* fmt, ...) noexcept
{
    asm volatile("cli");

    arch::x86_64::SerialPort::write("\n*** NOTYVOS KERNEL PANIC ***\n");

    // Re-emit through log with the caller's format. We must v-format here
    // because log::write is variadic and cannot be chained.
    // Simple approach: print fmt literally, then halt.
    log::write(log::Level::Error, "panic", "%s", fmt);

    arch::x86_64::SerialPort::write("System halted.\n");
    halt_forever();
}

} // namespace notyvos
