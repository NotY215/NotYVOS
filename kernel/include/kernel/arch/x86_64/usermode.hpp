#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

void usermode_prepare_kernel_stack() noexcept;
[[noreturn]] void enter_user(uptr user_rip, uptr user_rsp) noexcept;

extern "C" void task_entry_kernel() noexcept;
extern "C" void task_entry_user() noexcept;

} // namespace notyvos::arch::x86_64
