#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::tcp
{

struct Header
{
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8  data_off_flags_hi;
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} __attribute__((packed));

static_assert(sizeof(Header) == 20, "TCP header must be 20 bytes");

constexpr u8 kFlagFin = 0x01;
constexpr u8 kFlagSyn = 0x02;
constexpr u8 kFlagRst = 0x04;
constexpr u8 kFlagPsh = 0x08;
constexpr u8 kFlagAck = 0x10;

void init() noexcept;
void handle(Interface* iface, u32 src_ip, const u8* payload, usize len) noexcept;

} // namespace notyvos::net::tcp