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
// Warm re-init path: called on the second and subsequent kernel boots
// (i.e. after an ACPI restart). Skips the i8042 self-test because the
// controller is already powered and configured.
bool keyboard_reinit() noexcept;

// Alt-Tab event channel. Consumed by the compositor.
enum class AltTabEvent : u8
{
    None,
    Cycle,
    Commit
};
AltTabEvent keyboard_alt_tab_event() noexcept;
// True if the user pressed Alt+F4 since the last poll.
bool keyboard_alt_f4_event() noexcept;
// Clipboard shortcuts. Cleared by the consumer.
bool keyboard_ctrl_c_event() noexcept;
bool keyboard_ctrl_x_event() noexcept;
bool keyboard_ctrl_v_event() noexcept;
void keyboard_inject(char c) noexcept;

} // namespace notyvos::arch::x86_64
