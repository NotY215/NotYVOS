#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/ethernet.hpp>

namespace notyvos::net::arp
{

namespace
{
struct Entry
{
    u32 ip;
    Mac mac;
    u64 age;
    bool valid;
};

constexpr u32 kCacheSize = 16;
Entry g_cache[kCacheSize] = {};

} // namespace

void init() noexcept
{
    for (u32 i = 0; i < kCacheSize; ++i)
        g_cache[i].valid = false;
}

void insert(u32 ip, const Mac mac) noexcept
{
    for (u32 i = 0; i < kCacheSize; ++i)
    {
        if (g_cache[i].valid && g_cache[i].ip == ip)
        {
            libk::memcpy(g_cache[i].mac, mac, 6);
            g_cache[i].age = 0;
            return;
        }
    }
    for (u32 i = 0; i < kCacheSize; ++i)
    {
        if (!g_cache[i].valid)
        {
            g_cache[i].ip = ip;
            libk::memcpy(g_cache[i].mac, mac, 6);
            g_cache[i].age = 0;
            g_cache[i].valid = true;
            return;
        }
    }
    // Evict oldest.
    u32 oldest = 0;
    for (u32 i = 1; i < kCacheSize; ++i)
        if (g_cache[i].age > g_cache[oldest].age)
            oldest = i;
    g_cache[oldest].ip = ip;
    libk::memcpy(g_cache[oldest].mac, mac, 6);
    g_cache[oldest].age = 0;
    g_cache[oldest].valid = true;
}

bool lookup(u32 ip, Mac out) noexcept
{
    for (u32 i = 0; i < kCacheSize; ++i)
    {
        if (g_cache[i].valid && g_cache[i].ip == ip)
        {
            libk::memcpy(out, g_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

void request(Interface* iface, u32 ip) noexcept
{
    if (!iface)
        return;
    Packet p{};
    p.hw_type = htons(kHwEthernet);
    p.proto = htons(kProtoIPv4);
    p.hw_len = 6;
    p.proto_len = 4;
    p.op = htons(kOpRequest);
    libk::memcpy(p.sender_mac, iface->mac, 6);
    p.sender_ip = htonl(iface->ip);
    for (u32 i = 0; i < 6; ++i)
        p.target_mac[i] = 0;
    p.target_ip = htonl(ip);

    Mac bcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    (void)ether::send(iface, bcast, ether::kTypeARP, &p, sizeof(p));
}

void handle(Interface* iface, const u8* payload, usize len) noexcept
{
    if (len < sizeof(Packet))
        return;
    const auto* p = reinterpret_cast<const Packet*>(payload);
    if (ntohs(p->hw_type) != kHwEthernet || ntohs(p->proto) != kProtoIPv4)
        return;

    const u32 sender_ip = ntohl(p->sender_ip);
    const u32 target_ip = ntohl(p->target_ip);

    insert(sender_ip, p->sender_mac);

    if (ntohs(p->op) == kOpRequest && target_ip == iface->ip && iface->ip != 0)
    {
        // Reply.
        Packet r{};
        r.hw_type = htons(kHwEthernet);
        r.proto = htons(kProtoIPv4);
        r.hw_len = 6;
        r.proto_len = 4;
        r.op = htons(kOpReply);
        libk::memcpy(r.sender_mac, iface->mac, 6);
        r.sender_ip = htonl(iface->ip);
        libk::memcpy(r.target_mac, p->sender_mac, 6);
        r.target_ip = p->sender_ip;
        (void)ether::send(iface, p->sender_mac, ether::kTypeARP, &r, sizeof(r));
    }
}

} // namespace notyvos::net::arp
