#pragma once
#include <kernel/types.hpp>

namespace notyvos
{
[[noreturn]] void panic(const char* fmt, ...) noexcept;
} // namespace notyvos
