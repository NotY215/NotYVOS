#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/usb/hid.hpp>
#include <kernel/usb/xhci.hpp>
#include <kernel/input/input.hpp>

namespace notyvos::usb::hid
{

namespace
{

constexpr u32 kMaxDevices = 8;

struct HidDevice
{
    bool present;
    u32  ci;
    u8   slot_id;
    u8   ep_num;
    u8   protocol;      // 1 = keyboard, 2 = mouse
    u8   report_len;
    u32  poll_interval;
    u8   last_report[16];
};

HidDevice g_devices[kMaxDevices];
u32       g_device_count = 0;
u64       g_reports = 0;

// USB class code for HID.
constexpr u8 kClassHid        = 3;
constexpr u8 kSubclassBoot    = 1;
constexpr u8 kProtoKeyboard   = 1;
constexpr u8 kProtoMouse      = 2;

// Configuration descriptor header.
struct ConfigDescriptor
{
    u8  bLength;
    u8  bDescriptorType;
    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} __attribute__((packed));

struct InterfaceDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} __attribute__((packed));

struct EndpointDescriptor
{
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} __attribute__((packed));

// Search the configuration blob for a HID boot-protocol interface.
// Returns true if one was found and its fields extracted.
bool find_hid_interface(const u8* config, u16 config_len,
                        u8& out_iface_class,
                        u8& out_iface_subclass,
                        u8& out_iface_protocol,
                        u8& out_ep_num,
                        u16& out_ep_max_packet,
                        u8& out_ep_interval) noexcept
{
    u16 off = 0;
    bool found_iface = false;

    while (off + 2 <= config_len)
    {
        const u8 bLength = config[off];
        const u8 bType   = config[off + 1];
        if (bLength == 0) break;
        if (off + bLength > config_len) break;

        if (bType == 4 && bLength >= 9)   // Interface
        {
            const auto* id = reinterpret_cast<const InterfaceDescriptor*>(config + off);
            if (id->bInterfaceClass == kClassHid &&
                id->bInterfaceSubClass == kSubclassBoot &&
                (id->bInterfaceProtocol == kProtoKeyboard ||
                 id->bInterfaceProtocol == kProtoMouse))
            {
                out_iface_class    = id->bInterfaceClass;
                out_iface_subclass = id->bInterfaceSubClass;
                out_iface_protocol = id->bInterfaceProtocol;
                found_iface = true;
            }
            else
            {
                found_iface = false;
            }
        }
        else if (bType == 5 && bLength >= 7 && found_iface)   // Endpoint
        {
            const auto* ed = reinterpret_cast<const EndpointDescriptor*>(config + off);
            const u8 attr = ed->bmAttributes & 0x03u;
            const bool is_in = (ed->bEndpointAddress & 0x80u) != 0;
            if (attr == 3u && is_in)   // interrupt IN
            {
                out_ep_num        = static_cast<u8>(ed->bEndpointAddress & 0x0Fu);
                out_ep_max_packet = static_cast<u16>(ed->wMaxPacketSize & 0x07FFu);
                out_ep_interval   = ed->bInterval;
                return true;
            }
        }

        off = static_cast<u16>(off + bLength);
    }
    return false;
}

// Route one HID boot report to the OS input path.
void route_keyboard_report(const u8* report) noexcept
{
    // Boot keyboard report: [modifiers][reserved][key1..key6]
    // Compare against the previous report to detect new keys. We only
    // need to inject characters, so we send ASCII for the alphanumeric
    // keys and let the ring buffer dispatch.
    static u8 prev[8] = {0};
    const u8* cur = report;
    bool shift = (cur[0] & 0x22u) != 0;   // left or right shift

    for (u32 i = 2; i < 8; ++i)
    {
        const u8 k = cur[i];
        if (k == 0) continue;
        bool already = false;
        for (u32 j = 2; j < 8; ++j) if (prev[j] == k) { already = true; break; }
        if (already) continue;

        // Map USB HID usage code to ASCII. Only the common range 0x04..0x38.
        char c = 0;
        if (k >= 0x04u && k <= 0x1Du)   // a..z
            c = static_cast<char>('a' + (k - 0x04u));
        else if (k >= 0x1Eu && k <= 0x26u)  // 1..9
            c = static_cast<char>('1' + (k - 0x1Eu));
        else if (k == 0x27u) c = '0';
        else if (k == 0x28u) c = '\n';   // Enter
        else if (k == 0x2Cu) c = ' ';
        else if (k == 0x2Du) c = '-';
        else if (k == 0x2Eu) c = '=';
        else if (k == 0x2Fu) c = '[';
        else if (k == 0x30u) c = ']';
        else if (k == 0x31u) c = '\\';
        else if (k == 0x33u) c = ';';
        else if (k == 0x34u) c = '\'';
        else if (k == 0x35u) c = '`';
        else if (k == 0x36u) c = ',';
        else if (k == 0x37u) c = '.';
        else if (k == 0x38u) c = '/';

        if (c != 0)
        {
            if (shift && c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 32);
            input::keyboard::push(input::Source::Usb, c);
        }
    }

    for (u32 i = 0; i < 8; ++i) prev[i] = cur[i];
}

void route_mouse_report(const u8* report, u32 len) noexcept
{
    // Boot mouse report: [buttons][dx][dy][wheel?]
    if (len < 3)
        return;

    const u8 buttons = report[0];
    const i32 dx = static_cast<i32>(static_cast<i8>(report[1]));
    const i32 dy = static_cast<i32>(static_cast<i8>(report[2]));

    // USB reports +Y as up; screen-space Y grows downward.
    input::mouse::add_delta(input::Source::Usb, dx, -dy);

    input::mouse::set_button(input::Source::Usb, input::mouse::Button::Left,
                             (buttons & 0x01u) != 0);
    input::mouse::set_button(input::Source::Usb, input::mouse::Button::Right,
                             (buttons & 0x02u) != 0);
    input::mouse::set_button(input::Source::Usb, input::mouse::Button::Middle,
                             (buttons & 0x04u) != 0);

    if (len >= 4)
    {
        const i32 wz = static_cast<i32>(static_cast<i8>(report[3]));
        if (wz != 0)
            input::mouse::add_wheel(input::Source::Usb, wz);
    }
}

void bring_up_device(u32 ci, u8 slot_id,
                     u8 protocol, u8 ep_num, u16 ep_max_packet, u8 ep_interval) noexcept
{
    if (g_device_count >= kMaxDevices) return;

    // Choose a configuration value. We use 1 as a universal default;
    // configuration values 1 are mandatory.
    if (!xhci::set_configuration(ci, slot_id, 1))
    {
        log::write(log::Level::Warn, "hid",
                   "slot %llu: SET_CONFIGURATION failed",
                   static_cast<unsigned long long>(slot_id));
        return;
    }

    // Configure the interrupt IN endpoint.
    if (!xhci::configure_endpoint(ci, slot_id, ep_num,
                                  xhci::EpDir::In, xhci::EpType::Interrupt,
                                  ep_max_packet, ep_interval))
    {
        log::write(log::Level::Warn, "hid",
                   "slot %llu: Configure Endpoint failed",
                   static_cast<unsigned long long>(slot_id));
        return;
    }

    HidDevice& d = g_devices[g_device_count++];
    d.present       = true;
    d.ci            = ci;
    d.slot_id       = slot_id;
    d.ep_num        = ep_num;
    d.protocol      = protocol;
    d.report_len    = (protocol == kProtoKeyboard) ? 8u : 4u;
    d.poll_interval = ep_interval;
    libk::memset(d.last_report, 0, sizeof(d.last_report));

    log::write(log::Level::Info, "hid",
        "attached %s: ci=%llu slot=%llu ep=%llu max_packet=%llu interval=%llu",
        (protocol == kProtoKeyboard) ? "keyboard" : "mouse",
        static_cast<unsigned long long>(ci),
        static_cast<unsigned long long>(slot_id),
        static_cast<unsigned long long>(ep_num),
        static_cast<unsigned long long>(ep_max_packet),
        static_cast<unsigned long long>(ep_interval));
}

} // namespace

void init() noexcept
{
    g_device_count = 0;
    g_reports = 0;
    libk::memset(g_devices, 0, sizeof(g_devices));

    const u32 n_ctl = xhci::controller_count();
    if (n_ctl == 0)
    {
        log::write(log::Level::Info, "hid", "no USB controllers; HID skipped");
        return;
    }

    const u32 n_dev = xhci::device_count();
    for (u32 i = 0; i < n_dev; ++i)
    {
        const auto* dev = xhci::device(i);
        if (!dev) continue;

        // Fetch the first configuration descriptor.
        alignas(64) u8 cfg_buf[256];
        libk::memset(cfg_buf, 0, sizeof(cfg_buf));

        // Read just the 9-byte header first to get wTotalLength.
        const isize got_hdr = xhci::get_descriptor(dev->controller_index,
                                                    dev->slot_id,
                                                    2, 0, 0, cfg_buf, 9);
        if (got_hdr < 9) continue;

        const auto* cfg = reinterpret_cast<const ConfigDescriptor*>(cfg_buf);
        const u16 total = cfg->wTotalLength;
        if (total > sizeof(cfg_buf)) continue;

        const isize got_full = xhci::get_descriptor(dev->controller_index,
                                                     dev->slot_id,
                                                     2, 0, 0, cfg_buf, total);
        if (got_full < 9) continue;

        u8  iface_class = 0, iface_subclass = 0, iface_protocol = 0;
        u8  ep_num = 0, ep_interval = 0;
        u16 ep_max = 0;

        if (!find_hid_interface(cfg_buf, total,
                                iface_class, iface_subclass, iface_protocol,
                                ep_num, ep_max, ep_interval))
            continue;

        bring_up_device(dev->controller_index, dev->slot_id,
                        iface_protocol, ep_num, ep_max, ep_interval);
    }

    log::write(log::Level::Info, "hid",
               "HID init: %llu device(s)",
               static_cast<unsigned long long>(g_device_count));
}

void poll() noexcept
{
    for (u32 i = 0; i < g_device_count; ++i)
    {
        HidDevice& d = g_devices[i];
        if (!d.present) continue;

        alignas(64) u8 report[16];
        libk::memset(report, 0, sizeof(report));

        const isize n = xhci::interrupt_poll(d.ci, d.slot_id,
                                             d.ep_num, xhci::EpDir::In,
                                             report, d.report_len);
        if (n <= 0) continue;

        ++g_reports;
        if (d.protocol == kProtoKeyboard)
            route_keyboard_report(report);
        else if (d.protocol == kProtoMouse)
            route_mouse_report(report, static_cast<u32>(n));

        for (u32 b = 0; b < d.report_len && b < 16; ++b)
            d.last_report[b] = report[b];
    }
}

u32 keyboards() noexcept
{
    u32 n = 0;
    for (u32 i = 0; i < g_device_count; ++i)
        if (g_devices[i].present && g_devices[i].protocol == 1) ++n;
    return n;
}

u32 mice() noexcept
{
    u32 n = 0;
    for (u32 i = 0; i < g_device_count; ++i)
        if (g_devices[i].present && g_devices[i].protocol == 2) ++n;
    return n;
}

u64 reports_seen() noexcept { return g_reports; }

} // namespace notyvos::usb::hid
