#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net
{

bool e1000_init() noexcept;
bool e1000_present() noexcept;
Interface* e1000_interface() noexcept;

} // namespace notyvos::net