#pragma once
#include <kernel/types.hpp>

namespace notyvos::net
{

struct MacAddress
{
    u8 b[6];
};

bool e1000_init() noexcept;
bool e1000_present() noexcept;
MacAddress e1000_mac() noexcept;

} // namespace notyvos::net
