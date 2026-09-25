#pragma once
#include <kernel/types.hpp>

namespace notyvos::libk
{
usize strlen(const char* s) noexcept;
int strcmp(const char* a, const char* b) noexcept;
int strncmp(const char* a, const char* b, usize n) noexcept;
char* strcpy(char* dst, const char* src) noexcept;
char* strncpy(char* dst, const char* src, usize n) noexcept;
char* strcat(char* dst, const char* src) noexcept;
} // namespace notyvos::libk
