#pragma once
#include <kernel/net/net.hpp>
#include <kernel/net/wifi.hpp>

namespace notyvos::net::rtl8188eu
{

struct Device
{
    bool present;
    u32 ci;                // xHCI controller index
    u8  slot_id;
    u8  ep_in;
    u8  ep_out;
    u16 ep_in_max;
    u16 ep_out_max;
    u8  mac[6];
    u8  channel;
    u32 rx_packets;
    u32 tx_packets;

    // Adapter registered with the Wi-Fi framework.
    wifi::Adapter adapter;
};

void init() noexcept;
u32 device_count() noexcept;
Device* device(u32 index) noexcept;

// Called by the Wi-Fi framework when a scan is requested. Blocks until
// the scan completes or a 3-second timeout expires.
bool scan(Device* d) noexcept;

// Called by the Wi-Fi framework to associate with an SSID.
bool join(Device* d, const char* ssid, const u8* bssid) noexcept;

void disconnect(Device* d) noexcept;

// Called by the shell or the DHCP client to pump pending RX frames.
void poll() noexcept;

// Called from the IRQ-less USB poll loop.
void handle_usb_rx(Device* d, const u8* data, usize len) noexcept;

// Firmware loader entry point. Loads the firmware blob from the initramfs
// at /Firmware/rtl8188eufw.bin.
bool load_firmware(Device* d) noexcept;

} // namespace notyvos::net::rtl8188eu