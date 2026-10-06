#pragma once
#include <kernel/types.hpp>

namespace notyvos::usb::xhci
{

constexpr u32 kMaxControllers = 4;
constexpr u32 kMaxPorts = 32;
constexpr u32 kMaxDevices = 32;

enum class PortState : u8
{
    Disconnected = 0,
    Powered = 1,
    Enabled = 2,
    Reset = 3,
    Error = 4,
};

struct Controller
{
    bool present;
    u8 bus, slot, func;
    u64 mmio_phys;
    u8* mmio;

    u8 max_ports;
    u8 max_slots;
    u16 hci_version;
    bool started;

    u64 dcbaa_phys;
    u64 cmd_ring_phys;
    u64 event_ring_phys;
    u64 erst_phys;
};

// Public view of an enumerated device.
struct Device
{
    bool present;
    u32 controller_index;
    u8 slot_id;
    u8 port;
    u8 speed; // xHCI speed code: 1=FS 2=LS 3=HS 4=SS
    u8 device_class;
    u8 device_subclass;
    u8 device_protocol;
    u16 vendor_id;
    u16 product_id;
    u8 num_configurations;
};

// Enumerate PCI, bring up all xHCI controllers, then enumerate every
// attached device (Enable Slot, Address Device, GET_DESCRIPTOR for
// device + configuration).
void init() noexcept;

u32 controller_count() noexcept;
const Controller* controller(u32 index) noexcept;

u32 device_count() noexcept;
const Device* device(u32 index) noexcept;

PortState port_state(u32 controller_index, u8 port) noexcept;

// ---------------------------------------------------------------------------
// Extended API used by class drivers (HID, MSC).
// ---------------------------------------------------------------------------

enum class EpDir : u8
{
    Out = 0,
    In = 1
};
enum class EpType : u8
{
    Control = 0,
    Isoch = 1,
    Bulk = 2,
    Interrupt = 3
};

// Send SET_CONFIGURATION. Blocks until completion.
bool set_configuration(u32 ci, u8 slot_id, u8 config_value) noexcept;

// Configure one endpoint on an addressed slot. Uses the Configure Endpoint
// command. `ep_num` is 1..15 (endpoint number without direction bit).
bool configure_endpoint(u32 ci, u8 slot_id, u8 ep_num, EpDir dir, EpType type, u16 max_packet,
                        u8 interval) noexcept;

// Blocking bulk transfer. `data` must be a kernel VA in the HHDM-mapped
// region. Returns bytes transferred, or -1 on error.
isize bulk_transfer(u32 ci, u8 slot_id, u8 ep_num, EpDir dir, void* data, u32 length) noexcept;

// Non-blocking interrupt poll. Returns >0 if a report was received,
// 0 if no data is ready, -1 on error.
isize interrupt_poll(u32 ci, u8 slot_id, u8 ep_num, EpDir dir, void* data, u32 length) noexcept;

// Raw descriptor read helper. `desc_type` is the bDescriptorType field.
// `desc_index` is the descriptor index. Returns bytes read.
isize get_descriptor(u32 ci, u8 slot_id, u8 desc_type, u8 desc_index, u16 lang_id, void* data,
                     u16 length) noexcept;

} // namespace notyvos::usb::xhci
