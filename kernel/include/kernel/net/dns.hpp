#pragma once
#include <kernel/net/net.hpp>

namespace notyvos::net::dns
{

constexpr u32 kMaxNameLen = 256;

struct AddressList
{
    u32 v4[8];
    u32 count;
};

void init() noexcept;

// Set the DNS server. Defaults to whatever DHCP delivered.
void set_server(u32 server) noexcept;
u32  server() noexcept;

// Resolve `host` to one or more IPv4 addresses. Blocks up to
// `timeout_ms`.
bool resolve(const char* host, AddressList* out, u32 timeout_ms) noexcept;

// Called when a UDP datagram for the resolver's ephemeral port arrives.
void handle(const u8* payload, usize len) noexcept;

} // namespace notyvos::net::dns