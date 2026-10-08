#pragma once
#include <kernel/types.hpp>

namespace notyvos::net
{

using Mac = u8[6];

constexpr u32 kIfNameMax = 16;
constexpr u32 kMaxIfaces = 4;
constexpr usize kMaxFrame = 2048;

enum class IfType : u8
{
    Ethernet = 0,
    Wireless = 1,
    Loopback = 2,
};

enum class IfState : u8
{
    Down = 0,
    Up   = 1,
    LinkLost = 2,
};

using TxFn = bool (*)(void* user, const u8* frame, usize len);

struct Interface
{
    char name[kIfNameMax];
    IfType type;
    IfState state;
    Mac mac;
    u32 mtu;
    u32 ip;        // host byte order; 0 = unset
    u32 netmask;
    u32 gateway;
    u32 dns;
    TxFn tx;
    void* user;
    u64 rx_packets;
    u64 tx_packets;
    u64 rx_bytes;
    u64 tx_bytes;
    u64 rx_errors;
    u64 tx_errors;
};

void init() noexcept;
bool register_interface(Interface* iface) noexcept;
Interface* iface_by_index(u32 i) noexcept;
Interface* iface_by_name(const char* name) noexcept;
u32 iface_count() noexcept;

// Hand a received raw frame to the stack. Called from NIC drivers.
void receive_frame(Interface* iface, const u8* frame, usize len) noexcept;

// Send a raw frame out of an interface.
bool transmit(Interface* iface, const u8* frame, usize len) noexcept;

// Formatted helpers.
void format_mac(const Mac mac, char* out) noexcept;   // "aa:bb:cc:dd:ee:ff"
void format_ip(u32 ip, char* out) noexcept;           // "192.168.1.1"

// net-to-host helpers.
inline u16 ntohs(u16 v) noexcept { return static_cast<u16>((v >> 8) | (v << 8)); }
inline u16 htons(u16 v) noexcept { return ntohs(v); }
inline u32 ntohl(u32 v) noexcept
{
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}
inline u32 htonl(u32 v) noexcept { return ntohl(v); }

} // namespace notyvos::net