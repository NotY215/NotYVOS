#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::icmp
{

constexpr u8 kTypeEchoReply   = 0;
constexpr u8 kTypeEchoRequest = 8;

struct Header
{
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
} __attribute__((packed));

static_assert(sizeof(Header) == 8, "ICMP header must be 8 bytes");

void handle(Interface* iface, u32 src, const u8* payload, usize len) noexcept;

// Send an ICMP echo request.
void ping(Interface* iface, u32 dst, u16 id, u16 seq) noexcept;

u64 pings_sent() noexcept;
u64 pings_received() noexcept;
u32 last_ping_reply_ip() noexcept;

} // namespace notyvos::net::icmp