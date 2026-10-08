#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/dhcp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/icmp.hpp>
#include <kernel/net/ipv4.hpp>

namespace notyvos::net::ipv4
{

namespace
{
u16 g_ip_id = 1;

u16 ip_checksum(const Header* h) noexcept
{
    const u8* p = reinterpret_cast<const u8*>(h);
    u32 sum = 0;
    for (u32 i = 0; i < 20; i += 2)
        sum += static_cast<u32>(p[i]) << 8 | p[i + 1];
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<u16>(~sum & 0xFFFFu);
}

bool same_subnet(u32 a, u32 b, u32 mask) noexcept
{
    return (a & mask) == (b & mask);
}
} // namespace

void init() noexcept
{
    g_ip_id = 1;
}

Interface* route(u32 dst) noexcept
{
    for (u32 i = 0; i < iface_count(); ++i)
    {
        Interface* iface = iface_by_index(i);
        if (!iface || iface->state != IfState::Up || iface->ip == 0)
            continue;
        if (same_subnet(iface->ip, dst, iface->netmask))
            return iface;
    }
    // Fall back to any iface with a gateway.
    for (u32 i = 0; i < iface_count(); ++i)
    {
        Interface* iface = iface_by_index(i);
        if (iface && iface->state == IfState::Up && iface->ip != 0 && iface->gateway != 0)
            return iface;
    }
    return nullptr;
}

bool send(Interface* iface, u32 src, u32 dst, u8 proto, const void* payload, usize len) noexcept
{
    if (!iface || len + 20 > iface->mtu)
        return false;

    // Next hop is the destination if on-subnet, otherwise the gateway.
    u32 next_hop = same_subnet(src, dst, iface->netmask) ? dst : iface->gateway;
    Mac dst_mac{};
    if (!arp::lookup(next_hop, dst_mac))
    {
        arp::request(iface, next_hop);
        return false;   // caller should retry
    }

    u8 buf[kMaxFrame];
    auto* h = reinterpret_cast<Header*>(buf);
    h->version_ihl = 0x45;
    h->tos = 0;
    h->total_len = htons(static_cast<u16>(20 + len));
    h->id = htons(g_ip_id++);
    h->frag = htons(0x4000);   // don't fragment
    h->ttl = 64;
    h->proto = proto;
    h->checksum = 0;
    h->src = htonl(src);
    h->dst = htonl(dst);
    h->checksum = ip_checksum(h);
    libk::memcpy(buf + 20, payload, len);

    return ether::send(iface, dst_mac, ether::kTypeIPv4, buf, 20 + len);
}

void handle(Interface* iface, const u8* payload, usize len) noexcept
{
    if (len < 20)
        return;
    const auto* h = reinterpret_cast<const Header*>(payload);

    const u8 ihl = (h->version_ihl & 0x0Fu) * 4u;
    if (ihl < 20 || ihl > len)
        return;
    const u16 tot = ntohs(h->total_len);
    if (tot < ihl || tot > len)
        return;

    const u32 src = ntohl(h->src);
    const u32 dst = ntohl(h->dst);

    // Filter: accept broadcast or addressed to us.
    if (dst != iface->ip && dst != 0xFFFFFFFFu)
    {
        // Broadcast addresses depend on subnet; simple check:
        if ((dst & iface->netmask) != (iface->ip & iface->netmask))
            return;
    }

    const u8* next = payload + ihl;
    const usize nlen = tot - ihl;

    switch (h->proto)
    {
    case kProtoICMP:
        icmp::handle(iface, src, next, nlen);
        break;
    case kProtoUDP:
        dhcp::handle(iface, next, nlen);
        break;
    default:
        break;
    }
}

} // namespace notyvos::net::ipv4