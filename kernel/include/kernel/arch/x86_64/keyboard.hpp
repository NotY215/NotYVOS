#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

// Special key codes pushed into the ring buffer for non-printable keys.
// These values are above the ASCII range so they cannot collide.
constexpr char kKeyUp = static_cast<char>(0x80);
constexpr char kKeyDown = static_cast<char>(0x81);
constexpr char kKeyLeft = static_cast<char>(0x82);
constexpr char kKeyRight = static_cast<char>(0x83);
constexpr char kKeyHome = static_cast<char>(0x84);
constexpr char kKeyEnd = static_cast<char>(0x85);
constexpr char kKeyPgUp = static_cast<char>(0x86);
constexpr char kKeyPgDn = static_cast<char>(0x87);

bool keyboard_init() noexcept;
void keyboard_irq_handler() noexcept;
bool keyboard_poll() noexcept;
i32 keyboard_pop() noexcept;
bool keyboard_has_data() noexcept;
u64 keyboard_irq_count() noexcept;
void keyboard_inject(char c) noexcept;

} // namespace notyvos::arch::x86_64
