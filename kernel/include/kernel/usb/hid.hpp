#pragma once
#include <kernel/types.hpp>

namespace notyvos::usb::hid
{

// Enumerate HID boot-protocol keyboards and mice on all controllers.
// Attach any found to the existing keyboard/mouse input path.
void init() noexcept;

// Drain pending reports. Called from the compositor tick, or from a
// dedicated polling task if the scheduler has one.
void poll() noexcept;

// Stats.
u32 keyboards() noexcept;
u32 mice() noexcept;
u64 reports_seen() noexcept;

} // namespace notyvos::usb::hid