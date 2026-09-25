#include <kernel/panic.hpp>
#include <kernel/log.hpp>
#include <kernel/arch/x86_64/serial.hpp>

namespace notyvos {

[[noreturn]] void panic(const char* fmt, ...) noexcept {
    asm volatile("cli");

    arch::x86_64::SerialPort::write("\n*** NOTYVOS KERNEL PANIC ***\n");

    va_list ap;
    va_start(ap, fmt);
    // Route through log for formatting consistency.
    // (log::write is variadic; we forward as a single %s formatted string.)
    // In Phase 0, we accept the small duplication:
    char buf[512];
    // Minimal formatter into buf:
    // Reuse log's formatter by calling it with the user format but no tag.
    // To keep log.cpp self-contained we just print literal + fmt text here.
    // Phase 1 will unify this.
    (void)ap;
    log::write(log::Level::Error, "panic", "%s", fmt);
    va_end(ap);

    arch::x86_64::SerialPort::write("System halted.\n");
    halt_forever();
}

} // namespace notyvos