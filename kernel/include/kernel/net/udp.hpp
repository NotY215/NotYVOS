#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::udp
{

struct Header
{
    u16 src_port;
    u16 dst_port;
    u16 len;
    u16 checksum;
} __attribute__((packed));

static_assert(sizeof(Header) == 8, "UDP header must be 8 bytes");

using RecvFn = void (*)(void* user, u32 src_ip, u16 src_port, const u8* data, usize len);

bool bind(u16 port, RecvFn fn, void* user) noexcept;
void unbind(u16 port) noexcept;

bool send(Interface* iface, u32 src_ip, u16 src_port, u32 dst_ip, u16 dst_port, const void* data,
          usize len) noexcept;

void handle(Interface* iface, const u8* payload, usize len) noexcept;

} // namespace notyvos::net::udp
