#pragma once
#include <kernel/types.hpp>

namespace notyvos::proc
{
u32 alloc_pid() noexcept;
void free_pid(u32 pid) noexcept;
} // namespace notyvos::proc
