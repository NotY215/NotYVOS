#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::sock
{

enum class Domain : u8 { Inet = 2 };
enum class Type : u8 { Stream = 1, Dgram = 2 };
enum class Proto : u8 { Tcp = 6, Udp = 17 };

constexpr u32 kMaxSockets = 32;

struct Address
{
    u32 ip;
    u16 port;
};

struct Socket;
using AcceptCb = void (*)(void* user, Socket* child) noexcept;
using DataCb   = void (*)(void* user, const u8* data, usize len) noexcept;
using ErrorCb  = void (*)(void* user) noexcept;

struct Socket
{
    bool used;
    Domain domain;
    Type type;
    Proto proto;
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    bool bound;
    bool listening;
    bool connected;
    bool closed;

    void* user;
    AcceptCb on_accept;
    DataCb   on_data;
    ErrorCb  on_error;
};

void init() noexcept;

Socket* create(Domain d, Type t) noexcept;
void close(Socket* s) noexcept;
bool bind(Socket* s, u32 ip, u16 port) noexcept;
bool listen(Socket* s) noexcept;
Socket* accept(Socket* s) noexcept;
bool connect(Socket* s, u32 ip, u16 port) noexcept;
i64 send(Socket* s, const void* data, usize len) noexcept;
i64 recv(Socket* s, void* buf, usize len) noexcept;
void set_user(Socket* s, void* u) noexcept;
void set_callbacks(Socket* s, AcceptCb a, DataCb d, ErrorCb e) noexcept;

// Called from ipv4 dispatch.
void deliver_udp(u32 src_ip, u16 src_port, u16 dst_port, const u8* data, usize len) noexcept;
void deliver_tcp(u32 src_ip, u16 src_port, u16 dst_port, const u8* data, usize len) noexcept;

} // namespace notyvos::net::sock