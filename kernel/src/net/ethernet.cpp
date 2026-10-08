#include <kernel/libk/mem.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/net.hpp>

namespace notyvos::net::ether
{

bool send(Interface* iface, const Mac dst, u16 type_be, const void* payload, usize len) noexcept
{
    if (!iface || !payload || len + 14 > kMaxFrame)
        return false;
    u8 frame[kMaxFrame];
    auto* h = reinterpret_cast<Header*>(frame);
    libk::memcpy(h->dst, dst, 6);
    libk::memcpy(h->src, iface->mac, 6);
    h->type = type_be;
    libk::memcpy(frame + 14, payload, len);
    usize total = 14 + len;
    if (total < 60)
        total = 60;   // pad small frames
    return transmit(iface, frame, total);
}

void handle(Interface* iface, const u8* frame, usize len) noexcept
{
    if (len < 14)
        return;
    const auto* h = reinterpret_cast<const Header*>(frame);

    // Filter: accept broadcast, multicast not matched, or our MAC.
    const u16 type = h->type;
    const u8* payload = frame + 14;
    const usize plen = len - 14;

    if (type == htons(kTypeARP))
    {
        arp::handle(iface, payload, plen);
        return;
    }
    if (type == htons(kTypeIPv4))
    {
        ipv4::handle(iface, payload, plen);
        return;
    }
    // Ignore other ethertypes.
}

} // namespace notyvos::net::ether