#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

bool keyboard_init() noexcept;
void keyboard_irq_handler() noexcept;
bool keyboard_poll() noexcept;
i32 keyboard_pop() noexcept;
bool keyboard_has_data() noexcept;
u64 keyboard_irq_count() noexcept;
void keyboard_inject(char c) noexcept;

} // namespace notyvos::arch::x86_64
