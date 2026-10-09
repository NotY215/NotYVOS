#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::dhcp
{

void init() noexcept;

// Begin a DHCP DISCOVER exchange on `iface`. Blocks until an address is
// acquired or the timeout expires. Returns true on success.
bool acquire(Interface* iface, u32 timeout_ms) noexcept;

// Called from the IPv4 layer when a UDP datagram arrives for the DHCP
// client port.
void handle(Interface* iface, const u8* payload, usize len) noexcept;

} // namespace notyvos::net::dhcp
