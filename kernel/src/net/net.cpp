#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/dhcp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/icmp.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/net.hpp>

namespace notyvos::net
{

namespace
{
Interface* g_ifaces[kMaxIfaces] = {};
u32 g_count = 0;
}

void init() noexcept
{
    g_count = 0;
    for (u32 i = 0; i < kMaxIfaces; ++i)
        g_ifaces[i] = nullptr;
    arp::init();
    ipv4::init();
    dhcp::init();
    log::write(log::Level::Info, "net", "network stack initialized");
}

bool register_interface(Interface* iface) noexcept
{
    if (!iface || g_count >= kMaxIfaces)
        return false;
    g_ifaces[g_count++] = iface;
    log::write(log::Level::Info, "net", "registered %s (type=%u)", iface->name,
               static_cast<u64>(iface->type));
    return true;
}

Interface* iface_by_index(u32 i) noexcept
{
    return (i < g_count) ? g_ifaces[i] : nullptr;
}

Interface* iface_by_name(const char* name) noexcept
{
    if (!name)
        return nullptr;
    for (u32 i = 0; i < g_count; ++i)
        if (libk::strcmp(g_ifaces[i]->name, name) == 0)
            return g_ifaces[i];
    return nullptr;
}

u32 iface_count() noexcept
{
    return g_count;
}

void receive_frame(Interface* iface, const u8* frame, usize len) noexcept
{
    if (!iface || !frame || len < 14)
        return;
    ++iface->rx_packets;
    iface->rx_bytes += len;
    ether::handle(iface, frame, len);
}

bool transmit(Interface* iface, const u8* frame, usize len) noexcept
{
    if (!iface || !iface->tx || iface->state != IfState::Up)
        return false;
    if (!iface->tx(iface->user, frame, len))
    {
        ++iface->tx_errors;
        return false;
    }
    ++iface->tx_packets;
    iface->tx_bytes += len;
    return true;
}

void format_mac(const Mac mac, char* out) noexcept
{
    const char* hex = "0123456789abcdef";
    u32 p = 0;
    for (u32 i = 0; i < 6; ++i)
    {
        if (i > 0)
            out[p++] = ':';
        out[p++] = hex[(mac[i] >> 4) & 0xF];
        out[p++] = hex[mac[i] & 0xF];
    }
    out[p] = 0;
}

void format_ip(u32 ip, char* out) noexcept
{
    u32 p = 0;
    for (int sh = 24; sh >= 0; sh -= 8)
    {
        u32 v = (ip >> sh) & 0xFFu;
        if (v >= 100)
            out[p++] = static_cast<char>('0' + v / 100);
        if (v >= 10)
            out[p++] = static_cast<char>('0' + (v / 10) % 10);
        out[p++] = static_cast<char>('0' + v % 10);
        if (sh > 0)
            out[p++] = '.';
    }
    out[p] = 0;
}

} // namespace notyvos::net