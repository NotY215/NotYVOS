#pragma once
#include <kernel/types.hpp>

namespace notyvos::bt
{

namespace virtual_ctrl
{
void init() noexcept;
}

constexpr u32 kMaxDevices = 16;
constexpr u32 kMaxNameLen = 248;

enum class Transport : u8
{
    None = 0,
    Usb,
    Uart,
    Virtual,
};

enum class DeviceState : u8
{
    Down = 0,
    Powered,
    Scanning,
    Connected,
    Paired,
};

enum class DeviceClass : u8
{
    Unknown = 0,
    Computer,
    Phone,
    Audio,
    Keyboard,
    Mouse,
    Headset,
    Printer,
    Other,
};

struct Address
{
    u8 b[6];
};

struct Device
{
    Address address;
    char name[kMaxNameLen];
    DeviceClass cls;
    i8  rssi;
    bool paired;
    bool connected;
    bool le;   // Low Energy
};

void init() noexcept;
u32 device_count() noexcept;
const Device* device(u32 index) noexcept;

// HCI transport registration. Drivers (USB, UART, virtual) call this.
using HciSendFn = bool (*)(void* user, const u8* data, usize len);
void register_transport(Transport t, HciSendFn send, void* user) noexcept;
Transport current_transport() noexcept;
bool powered() noexcept;

// HCI bring-up: reset, read BD_ADDR, set event mask, set local name.
bool power_on() noexcept;
void power_off() noexcept;

// Discovery. Blocks with a timeout.
bool start_inquiry(u32 timeout_ms) noexcept;
void stop_inquiry() noexcept;
bool inquiry_active() noexcept;

// Pairing / connection.
bool pair(const Address& addr) noexcept;
bool connect(const Address& addr) noexcept;
void disconnect(const Address& addr) noexcept;

// Called by the transport driver when data arrives from the controller.
void hci_receive(const u8* data, usize len) noexcept;

// Periodic tick: pumps inquiry timers, retries, keep-alives.
void tick() noexcept;

// Diagnostics.
u64 inquiries_started() noexcept;
u64 devices_found() noexcept;

void format_address(const Address& a, char* out) noexcept;

} // namespace notyvos::bt
