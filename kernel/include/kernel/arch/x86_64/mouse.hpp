#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

bool mouse_init() noexcept;
void mouse_irq_handler() noexcept;

i32 mouse_x() noexcept;
i32 mouse_y() noexcept;
i32 mouse_wheel() noexcept; // accumulates; callers read and clear
void mouse_wheel_clear() noexcept;

bool mouse_left() noexcept;
bool mouse_right() noexcept;
bool mouse_middle() noexcept;

void mouse_set_position(i32 x, i32 y) noexcept;

} // namespace notyvos::arch::x86_64
