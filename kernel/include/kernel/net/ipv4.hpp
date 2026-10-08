#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::ipv4
{

constexpr u8 kProtoICMP = 1;
constexpr u8 kProtoUDP  = 17;
constexpr u8 kProtoTCP  = 6;

struct Header
{
    u8  version_ihl;
    u8  tos;
    u16 total_len;
    u16 id;
    u16 frag;
    u8  ttl;
    u8  proto;
    u16 checksum;
    u32 src;
    u32 dst;
} __attribute__((packed));

static_assert(sizeof(Header) == 20, "IPv4 header must be 20 bytes");

void init() noexcept;

// Send an IPv4 packet.
bool send(Interface* iface, u32 src, u32 dst, u8 proto,
          const void* payload, usize len) noexcept;

void handle(Interface* iface, const u8* payload, usize len) noexcept;

// Identify the interface that should carry traffic for `dst`.
Interface* route(u32 dst) noexcept;

} // namespace notyvos::net::ipv4