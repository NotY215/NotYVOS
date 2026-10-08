#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/net/udp.hpp>

namespace notyvos::net::sock
{

namespace
{
Socket g_sockets[kMaxSockets] = {};

Socket* find_udp(u16 port) noexcept
{
    for (u32 i = 0; i < kMaxSockets; ++i)
    {
        Socket& s = g_sockets[i];
        if (!s.used || s.proto != Proto::Udp) continue;
        if (s.listening || s.bound)
        {
            if (s.local_port == port) return &s;
        }
    }
    return nullptr;
}

void udp_relay(void* user, u32 /*src_ip*/, u16 src_port, const u8* data, usize len) noexcept
{
    auto* s = static_cast<Socket*>(user);
    if (!s || !s->on_data) return;
    s->remote_port = src_port;
    s->on_data(s->user, data, len);
}
} // namespace

void init() noexcept
{
    libk::memset(g_sockets, 0, sizeof(g_sockets));
}

Socket* create(Domain d, Type t) noexcept
{
    for (u32 i = 0; i < kMaxSockets; ++i)
    {
        if (!g_sockets[i].used)
        {
            Socket& s = g_sockets[i];
            libk::memset(&s, 0, sizeof(s));
            s.used = true;
            s.domain = d;
            s.type = t;
            s.proto = (t == Type::Dgram) ? Proto::Udp : Proto::Tcp;
            return &s;
        }
    }
    return nullptr;
}

void close(Socket* s) noexcept
{
    if (!s) return;
    if (s->proto == Proto::Udp && s->bound)
        udp::unbind(s->local_port);
    s->used = false;
}

bool bind(Socket* s, u32 ip, u16 port) noexcept
{
    if (!s) return false;
    s->local_ip = ip;
    s->local_port = port;
    s->bound = true;
    if (s->proto == Proto::Udp)
        return udp::bind(port, udp_relay, s);
    return true;
}

bool listen(Socket* s) noexcept
{
    if (!s || s->proto != Proto::Tcp) return false;
    s->listening = true;
    return true;
}

Socket* accept(Socket*) noexcept
{
    // Simplified: TCP handshake handling for inbound connections is not
    // yet implemented. Return null so callers fall back.
    return nullptr;
}

bool connect(Socket* s, u32 ip, u16 port) noexcept
{
    if (!s) return false;
    s->remote_ip = ip;
    s->remote_port = port;
    s->connected = true;
    return true;
}

i64 send(Socket* s, const void* data, usize len) noexcept
{
    if (!s || !s->connected) return -1;
    auto* iface = ipv4::route(s->remote_ip);
    if (!iface) return -1;
    if (s->proto == Proto::Udp)
    {
        if (!udp::send(iface, s->local_ip ? s->local_ip : iface->ip, s->local_port,
                       s->remote_ip, s->remote_port, data, len))
            return -1;
        return static_cast<i64>(len);
    }
    // TCP: not implemented in this baseline. Returns -1.
    return -1;
}

i64 recv(Socket*, void*, usize) noexcept
{
    // Data delivery is via callbacks in this baseline. Synchronous recv
    // requires a ring buffer per socket, which arrives with the TCP
    // implementation.
    return -1;
}

void set_user(Socket* s, void* u) noexcept { if (s) s->user = u; }
void set_callbacks(Socket* s, AcceptCb a, DataCb d, ErrorCb e) noexcept
{
    if (!s) return;
    s->on_accept = a; s->on_data = d; s->on_error = e;
}

void deliver_udp(u32 src_ip, u16 src_port, u16 dst_port, const u8* data, usize len) noexcept
{
    Socket* s = find_udp(dst_port);
    if (!s) return;
    s->remote_ip = src_ip;
    s->remote_port = src_port;
    if (s->on_data) s->on_data(s->user, data, len);
}

void deliver_tcp(u32, u16, u16, const u8*, usize) noexcept
{
    // TCP input handling is deferred.
}

} // namespace notyvos::net::sock