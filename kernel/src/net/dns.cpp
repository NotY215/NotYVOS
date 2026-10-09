#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/net/dns.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/net.hpp>
#include <kernel/net/udp.hpp>

namespace notyvos::net::dns
{

extern "C" void notyvos_e1000_poll() noexcept;
extern "C" notyvos::u64 notyvos_net_now_ticks() noexcept;

namespace
{

constexpr u16 kDnsPort = 53;
u16 g_local_port = 53535;
u32 g_server = 0;
u32 g_query_id = 0x4E590000u;

AddressList g_result = {};
bool g_have_result = false;
u32 g_expected_id = 0;

u32 encode_name(const char* host, u8* out, usize cap) noexcept
{
    u32 pos = 0;
    const char* p = host;
    while (*p)
    {
        const char* seg_start = p;
        while (*p && *p != '.')
            ++p;
        const u32 seg_len = static_cast<u32>(p - seg_start);
        if (seg_len == 0 || seg_len > 63)
            return 0;
        if (pos + 1 + seg_len >= cap)
            return 0;
        out[pos++] = static_cast<u8>(seg_len);
        for (u32 i = 0; i < seg_len; ++i)
            out[pos++] = static_cast<u8>(seg_start[i]);
        if (*p == '.')
            ++p;
    }
    if (pos >= cap)
        return 0;
    out[pos++] = 0;
    return pos;
}

void on_udp(void* /*user*/, u32 /*src_ip*/, u16 /*src_port*/, const u8* data, usize len) noexcept
{
    handle(data, len);
}

} // namespace

void init() noexcept
{
    libk::memset(&g_result, 0, sizeof(g_result));
    g_have_result = false;
    (void)udp::bind(g_local_port, on_udp, nullptr);
    log::write(log::Level::Info, "dns", "resolver ready");
}

void set_server(u32 s) noexcept
{
    g_server = s;
}
u32 server() noexcept
{
    return g_server;
}

bool resolve(const char* host, AddressList* out, u32 timeout_ms) noexcept
{
    if (!host || !out)
        return false;
    if (g_server == 0)
    {
        auto* iface = ipv4::route(0);
        if (iface)
        {
            if (iface->dns)
                g_server = iface->dns;
            else if (iface->gateway)
                g_server = iface->gateway;
        }
    }
    if (g_server == 0)
        return false;

    u8 query[512];
    u32 pos = 0;
    const u32 id = ++g_query_id;
    g_expected_id = id;
    query[pos++] = static_cast<u8>((id >> 8) & 0xFF);
    query[pos++] = static_cast<u8>(id & 0xFF);
    query[pos++] = 0x01;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;
    query[pos++] = 0x00;

    const u32 name_len = encode_name(host, query + pos, sizeof(query) - pos - 4);
    if (name_len == 0)
        return false;
    pos += name_len;
    query[pos++] = 0x00;
    query[pos++] = 0x01;
    query[pos++] = 0x00;
    query[pos++] = 0x01;

    auto* iface = ipv4::route(g_server);
    if (!iface)
        return false;
    if (!udp::send(iface, iface->ip, g_local_port, g_server, kDnsPort, query, pos))
        return false;

    const u64 deadline = static_cast<u64>(timeout_ms) * 100ULL + 1ULL;
    const u64 t0 = notyvos_net_now_ticks();
    while (!g_have_result)
    {
        notyvos_e1000_poll();
        if (notyvos_net_now_ticks() - t0 > deadline)
            return false;
        asm volatile("pause");
    }

    *out = g_result;
    g_have_result = false;
    return out->count > 0;
}

void handle(const u8* payload, usize len) noexcept
{
    if (len < 12)
        return;
    const u16 id = static_cast<u16>((payload[0] << 8) | payload[1]);
    if (id != g_expected_id)
        return;
    if ((payload[2] & 0x80u) == 0)
        return;
    const u16 qdcount = static_cast<u16>((payload[4] << 8) | payload[5]);
    const u16 ancount = static_cast<u16>((payload[6] << 8) | payload[7]);
    usize pos = 12;

    for (u16 q = 0; q < qdcount && pos + 5 < len; ++q)
    {
        while (pos < len && payload[pos] != 0)
        {
            if ((payload[pos] & 0xC0u) == 0xC0u)
            {
                pos += 2;
                goto q_done;
            }
            pos += 1 + payload[pos];
        }
        pos += 1;
    q_done:
        pos += 4;
    }

    g_result.count = 0;
    for (u16 a = 0; a < ancount && pos + 10 < len; ++a)
    {
        if ((payload[pos] & 0xC0u) == 0xC0u)
        {
            pos += 2;
        }
        else
        {
            while (pos < len && payload[pos] != 0)
                pos += 1 + payload[pos];
            pos += 1;
        }
        if (pos + 10 > len)
            break;
        const u16 type = static_cast<u16>((payload[pos] << 8) | payload[pos + 1]);
        const u16 cls = static_cast<u16>((payload[pos + 2] << 8) | payload[pos + 3]);
        const u16 rdlen = static_cast<u16>((payload[pos + 8] << 8) | payload[pos + 9]);
        pos += 10;
        if (pos + rdlen > len)
            break;

        if (type == 1 && cls == 1 && rdlen == 4 && g_result.count < 8)
        {
            const u32 ip = (static_cast<u32>(payload[pos]) << 24) |
                           (static_cast<u32>(payload[pos + 1]) << 16) |
                           (static_cast<u32>(payload[pos + 2]) << 8) |
                           static_cast<u32>(payload[pos + 3]);
            g_result.v4[g_result.count++] = ip;
        }
        pos += rdlen;
    }

    if (g_result.count > 0)
    {
        char ipbuf[20];
        format_ip(g_result.v4[0], ipbuf);
        log::write(log::Level::Info, "dns", "resolved -> %s", ipbuf);
    }
    g_have_result = true;
}

} // namespace notyvos::net::dns
