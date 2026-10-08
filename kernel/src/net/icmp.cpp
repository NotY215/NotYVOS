#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/net/icmp.hpp>
#include <kernel/net/ipv4.hpp>

namespace notyvos::net::icmp
{

namespace
{
u64 g_sent = 0;
u64 g_recv = 0;
u32 g_last_ip = 0;

u16 checksum(const void* data, usize len) noexcept
{
    const u8* p = static_cast<const u8*>(data);
    u32 sum = 0;
    usize i = 0;
    for (; i + 1 < len; i += 2)
        sum += static_cast<u32>(p[i]) << 8 | p[i + 1];
    if (i < len)
        sum += static_cast<u32>(p[i]) << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<u16>(~sum & 0xFFFFu);
}
} // namespace

void handle(Interface* iface, u32 src, const u8* payload, usize len) noexcept
{
    if (len < sizeof(Header))
        return;
    const auto* h = reinterpret_cast<const Header*>(payload);

    if (h->type == kTypeEchoRequest)
    {
        // Reply with the same body.
        u8 buf[kMaxFrame];
        const usize body_len = len;
        if (body_len > sizeof(buf))
            return;
        libk::memcpy(buf, payload, body_len);
        auto* r = reinterpret_cast<Header*>(buf);
        r->type = kTypeEchoReply;
        r->code = 0;
        r->checksum = 0;
        r->checksum = checksum(buf, body_len);
        (void)ipv4::send(iface, iface->ip, src, ipv4::kProtoICMP, buf, body_len);
    }
    else if (h->type == kTypeEchoReply)
    {
        ++g_recv;
        g_last_ip = src;
        log::write(log::Level::Info, "icmp", "echo reply from %u.%u.%u.%u seq=%u",
                   static_cast<u64>((src >> 24) & 0xFF), static_cast<u64>((src >> 16) & 0xFF),
                   static_cast<u64>((src >> 8) & 0xFF), static_cast<u64>(src & 0xFF),
                   static_cast<u64>(ntohs(h->seq)));
    }
}

void ping(Interface* iface, u32 dst, u16 id, u16 seq) noexcept
{
    Header h{};
    h.type = kTypeEchoRequest;
    h.code = 0;
    h.id = htons(id);
    h.seq = htons(seq);
    h.checksum = checksum(&h, sizeof(h));
    if (ipv4::send(iface, iface->ip, dst, ipv4::kProtoICMP, &h, sizeof(h)))
        ++g_sent;
}

u64 pings_sent() noexcept { return g_sent; }
u64 pings_received() noexcept { return g_recv; }
u32 last_ping_reply_ip() noexcept { return g_last_ip; }

} // namespace notyvos::net::icmp