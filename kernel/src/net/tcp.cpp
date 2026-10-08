#include <kernel/log.hpp>
#include <kernel/net/tcp.hpp>

namespace notyvos::net::tcp
{

void init() noexcept
{
    log::write(log::Level::Info, "tcp", "TCP stub ready (handshake deferred)");
}

void handle(Interface*, u32, const u8*, usize) noexcept
{
    // Inbound TCP is not yet handled. The UDP path plus the DHCP client
    // cover the current milestone (link up + address acquisition).
}

} // namespace notyvos::net::tcp