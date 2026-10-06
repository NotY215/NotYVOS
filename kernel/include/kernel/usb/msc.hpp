#pragma once
#include <kernel/types.hpp>

namespace notyvos::usb::msc
{

// Enumerate Mass Storage interfaces, register each as a block device.
void init() noexcept;

// Stats.
u32 devices() noexcept;

} // namespace notyvos::usb::msc