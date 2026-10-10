#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/net/dhcp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>

namespace notyvos::net::dhcp
{

extern "C" notyvos::u64 notyvos_net_now_ticks() noexcept;

namespace
{

constexpr u8 kOpRequest = 1;
constexpr u8 kOpReply = 2;

constexpr u8 kMsgDiscover = 1;
constexpr u8 kMsgOffer = 2;
constexpr u8 kMsgRequest = 3;
constexpr u8 kMsgAck = 5;

struct Header
{
    u8 op;
    u8 htype;
    u8 hlen;
    u8 hops;
    u32 xid;
    u16 secs;
    u16 flags;
    u32 ciaddr;
    u32 yiaddr;
    u32 siaddr;
    u32 giaddr;
    u8 chaddr[16];
    u8 sname[64];
    u8 file[128];
    u32 magic;
    u8 options[64];
} __attribute__((packed));

u32 g_xid = 0x4E59564F;
u32 g_offered_ip = 0;
u32 g_server_ip = 0;
bool g_has_offer = false;
bool g_acked = false;

bool broadcast(Interface* iface, const Header& h) noexcept
{
    Mac bcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return ether::send(iface, bcast, ether::kTypeIPv4, &h, sizeof(h));
}

} // namespace

void init() noexcept
{
    g_has_offer = false;
    g_acked = false;
    g_offered_ip = 0;
    g_server_ip = 0;
}

void handle(Interface* iface, const u8* payload, usize len) noexcept
{
    if (len < sizeof(Header))
        return;
    const auto* h = reinterpret_cast<const Header*>(payload);
    if (h->op != kOpReply || h->xid != htonl(g_xid))
        return;

    const u8* opt = payload + offsetof(Header, options);
    const usize opt_len = len - offsetof(Header, options);
    u8 msg = 0;
    u32 subnet = 0;
    u32 router = 0;
    u32 dns = 0;
    usize i = 0;
    while (i + 2 <= opt_len)
    {
        const u8 code = opt[i];
        if (code == 0)
        {
            ++i;
            continue;
        }
        if (code == 255)
            break;
        const u8 olen = opt[i + 1];
        if (i + 2 + olen > opt_len)
            break;
        if (code == 53 && olen >= 1)
            msg = opt[i + 2];
        if (code == 1 && olen >= 4)
            libk::memcpy(&subnet, opt + i + 2, 4);
        if (code == 3 && olen >= 4)
            libk::memcpy(&router, opt + i + 2, 4);
        if (code == 6 && olen >= 4)
            libk::memcpy(&dns, opt + i + 2, 4);
        i += 2 + olen;
    }

    if (msg == kMsgOffer)
    {
        g_offered_ip = ntohl(h->yiaddr);
        g_server_ip = ntohl(h->siaddr);
        g_has_offer = true;

        // Immediately request the offered address. Doing this from the
        // RX path keeps acquire() non-blocking and lets the handshake
        // complete asynchronously once the PIT is delivering IRQs.
        Header r{};
        r.op = kOpRequest;
        r.htype = 1;
        r.hlen = 6;
        r.xid = htonl(g_xid);
        r.flags = htons(0x8000);
        libk::memcpy(r.chaddr, iface->mac, 6);
        r.magic = htonl(0x63825363);
        u8 ropts[16] = {53, 1, kMsgRequest, 50, 4, 0, 0, 0, 0, 54, 4, 0, 0, 0, 0, 255};
        ropts[5] = static_cast<u8>((g_offered_ip >> 24) & 0xFF);
        ropts[6] = static_cast<u8>((g_offered_ip >> 16) & 0xFF);
        ropts[7] = static_cast<u8>((g_offered_ip >> 8) & 0xFF);
        ropts[8] = static_cast<u8>(g_offered_ip & 0xFF);
        ropts[11] = static_cast<u8>((g_server_ip >> 24) & 0xFF);
        ropts[12] = static_cast<u8>((g_server_ip >> 16) & 0xFF);
        ropts[13] = static_cast<u8>((g_server_ip >> 8) & 0xFF);
        ropts[14] = static_cast<u8>(g_server_ip & 0xFF);
        libk::memcpy(r.options, ropts, sizeof(ropts));
        (void)broadcast(iface, r);
    }
    else if (msg == kMsgAck)
    {
        iface->ip = ntohl(h->yiaddr);
        iface->netmask = subnet ? ntohl(subnet) : 0xFFFFFF00u;
        iface->gateway = router ? ntohl(router) : 0;
        iface->dns = dns ? ntohl(dns) : 0;
        g_acked = true;
        char ipbuf[20];
        format_ip(iface->ip, ipbuf);
        log::write(log::Level::Info, "dhcp", "%s acquired %s", iface->name, ipbuf);
    }
}

bool acquire(Interface* iface, u32 /*timeout_ms*/) noexcept
{
    if (!iface)
        return false;
    init();

    Header h{};
    h.op = kOpRequest;
    h.htype = 1;
    h.hlen = 6;
    h.xid = htonl(g_xid);
    h.flags = htons(0x8000);
    libk::memcpy(h.chaddr, iface->mac, 6);
    h.magic = htonl(0x63825363);
    u8 opts[4] = {53, 1, kMsgDiscover, 255};
    libk::memcpy(h.options, opts, 4);

    if (!broadcast(iface, h))
        return false;

    log::write(log::Level::Info, "dhcp", "%s discover sent", iface->name);
    return true;
}

} // namespace notyvos::net::dhcp
