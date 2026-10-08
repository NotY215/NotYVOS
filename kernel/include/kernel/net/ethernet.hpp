#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::ether
{

constexpr u16 kTypeIPv4 = 0x0800;
constexpr u16 kTypeARP  = 0x0806;
constexpr u16 kTypeIPv6 = 0x86DD;

struct Header
{
    u8  dst[6];
    u8  src[6];
    u16 type;   // network byte order
} __attribute__((packed));

static_assert(sizeof(Header) == 14, "ethernet header must be 14 bytes");

bool send(Interface* iface, const Mac dst, u16 type_be, const void* payload, usize len) noexcept;
void handle(Interface* iface, const u8* frame, usize len) noexcept;

} // namespace notyvos::net::ether