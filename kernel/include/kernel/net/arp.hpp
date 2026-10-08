#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::arp
{

constexpr u16 kHwEthernet = 1;
constexpr u16 kProtoIPv4  = 0x0800;
constexpr u8  kOpRequest  = 1;
constexpr u8  kOpReply    = 2;

struct Packet
{
    u16 hw_type;
    u16 proto;
    u8  hw_len;
    u8  proto_len;
    u16 op;
    u8  sender_mac[6];
    u32 sender_ip;
    u8  target_mac[6];
    u32 target_ip;
} __attribute__((packed));

static_assert(sizeof(Packet) == 28, "ARP packet must be 28 bytes");

void init() noexcept;
void handle(Interface* iface, const u8* payload, usize len) noexcept;

// Send an ARP request for `ip` on `iface`.
void request(Interface* iface, u32 ip) noexcept;

// Look up a MAC. Returns true on cache hit; fills `out`.
bool lookup(u32 ip, Mac out) noexcept;

// Insert or update an entry.
void insert(u32 ip, const Mac mac) noexcept;

} // namespace notyvos::net::arp