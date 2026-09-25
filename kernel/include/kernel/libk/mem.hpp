#pragma once
#include <kernel/types.hpp>

namespace notyvos::libk
{
void* memcpy(void* dst, const void* src, usize n) noexcept;
void* memmove(void* dst, const void* src, usize n) noexcept;
void* memset(void* dst, int c, usize n) noexcept;
int memcmp(const void* a, const void* b, usize n) noexcept;
} // namespace notyvos::libk
