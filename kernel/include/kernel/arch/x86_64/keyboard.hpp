#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

// Full PS/2 controller initialization per OSDev wiki.
// Returns true on success.
bool keyboard_init() noexcept;

void keyboard_irq_handler() noexcept;
bool keyboard_poll() noexcept;
i32 keyboard_pop() noexcept;
bool keyboard_has_data() noexcept;
u64 keyboard_irq_count() noexcept;

// Inject a character from a non-PS/2 source (e.g. COM1 serial).
void keyboard_inject(char c) noexcept;

} // namespace notyvos::arch::x86_64
