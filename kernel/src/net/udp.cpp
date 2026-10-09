#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/udp.hpp>

namespace notyvos::net::udp
{

namespace
{

struct Slot
{
    u16 port;
    RecvFn fn;
    void* user;
    bool used;
};

constexpr u32 kSlots = 16;
Slot g_slots[kSlots] = {};

u16 checksum_pseudo(u32 src, u32 dst, u8 proto, const u8* data, usize len) noexcept
{
    u32 sum = 0;
    sum += (src >> 16) & 0xFFFFu;
    sum += src & 0xFFFFu;
    sum += (dst >> 16) & 0xFFFFu;
    sum += dst & 0xFFFFu;
    sum += proto;
    sum += static_cast<u32>(len);
    usize i = 0;
    for (; i + 1 < len; i += 2)
        sum += (static_cast<u32>(data[i]) << 8) | data[i + 1];
    if (i < len)
        sum += static_cast<u32>(data[i]) << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<u16>(~sum & 0xFFFFu);
}

} // namespace

bool bind(u16 port, RecvFn fn, void* user) noexcept
{
    for (u32 i = 0; i < kSlots; ++i)
    {
        if (!g_slots[i].used)
        {
            g_slots[i].port = port;
            g_slots[i].fn = fn;
            g_slots[i].user = user;
            g_slots[i].used = true;
            return true;
        }
    }
    return false;
}

void unbind(u16 port) noexcept
{
    for (u32 i = 0; i < kSlots; ++i)
    {
        if (g_slots[i].used && g_slots[i].port == port)
            g_slots[i].used = false;
    }
}

bool send(Interface* iface, u32 src_ip, u16 src_port, u32 dst_ip, u16 dst_port, const void* data,
          usize len) noexcept
{
    if (!iface || len + 8 > iface->mtu)
        return false;

    u8 buf[kMaxFrame];
    auto* h = reinterpret_cast<Header*>(buf);
    h->src_port = htons(src_port);
    h->dst_port = htons(dst_port);
    h->len = htons(static_cast<u16>(8 + len));
    h->checksum = 0;
    libk::memcpy(buf + 8, data, len);
    h->checksum = checksum_pseudo(src_ip, dst_ip, ipv4::kProtoUDP, buf, 8 + len);

    return ipv4::send(iface, src_ip, dst_ip, ipv4::kProtoUDP, buf, 8 + len);
}

void handle(Interface* /*iface*/, const u8* payload, usize len) noexcept
{
    if (len < 8)
        return;

    const auto* h = reinterpret_cast<const Header*>(payload);
    const u16 dport = ntohs(h->dst_port);
    const u16 sport = ntohs(h->src_port);
    const u16 ulen = ntohs(h->len);
    if (ulen < 8 || ulen > len)
        return;

    for (u32 i = 0; i < kSlots; ++i)
    {
        if (g_slots[i].used && g_slots[i].port == dport)
        {
            g_slots[i].fn(g_slots[i].user, 0, sport, payload + 8, ulen - 8);
            return;
        }
    }
}

} // namespace notyvos::net::udp
