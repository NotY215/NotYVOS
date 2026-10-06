#include <kernel/log.hpp>
#include <kernel/usb/hid.hpp>
#include <kernel/usb/self_test.hpp>
#include <kernel/usb/xhci.hpp>
#include <kernel/usb/msc.hpp>

namespace notyvos::usb
{

void self_test() noexcept
{
    const u32 nc = xhci::controller_count();
    const u32 nd = xhci::device_count();
    const u32 kb = hid::keyboards();
    const u32 ms = hid::mice();
    const u32 sc = msc::devices();

    log::write(log::Level::Info, "usb", "  mass storage devices: %llu",
               static_cast<unsigned long long>(sc));

    log::write(nc > 0 ? log::Level::Info : log::Level::Warn, "usb",
               "USB stack: %llu controller(s), %llu device(s), %llu kbd, %llu mouse",
               static_cast<unsigned long long>(nc), static_cast<unsigned long long>(nd),
               static_cast<unsigned long long>(kb), static_cast<unsigned long long>(ms));

    for (u32 i = 0; i < nd; ++i)
    {
        const auto* d = xhci::device(i);
        if (!d)
            continue;
        log::write(log::Level::Info, "usb",
                   "  device %llu: slot=%llu port=%llu vid=0x%04llx pid=0x%04llx class=0x%02llx",
                   static_cast<unsigned long long>(i), static_cast<unsigned long long>(d->slot_id),
                   static_cast<unsigned long long>(d->port),
                   static_cast<unsigned long long>(d->vendor_id),
                   static_cast<unsigned long long>(d->product_id),
                   static_cast<unsigned long long>(d->device_class));
    }

    log::write(log::Level::Info, "usb", "USB self-test complete");
}

} // namespace notyvos::usb
