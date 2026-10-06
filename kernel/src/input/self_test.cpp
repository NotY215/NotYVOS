#include <kernel/input/input.hpp>
#include <kernel/input/self_test.hpp>
#include <kernel/log.hpp>

namespace notyvos::input
{

void self_test() noexcept
{
    const u64 me = mouse::events();
    const u64 mp = mouse::ps2_events();
    const u64 mu = mouse::usb_events();

    const u64 ke = keyboard::events();
    const u64 kp = keyboard::ps2_events();
    const u64 ku = keyboard::usb_events();

    log::write(log::Level::Info, "input",
        "mouse events=%llu (ps2=%llu usb=%llu)  "
        "keyboard events=%llu (ps2=%llu usb=%llu)",
        static_cast<unsigned long long>(me),
        static_cast<unsigned long long>(mp),
        static_cast<unsigned long long>(mu),
        static_cast<unsigned long long>(ke),
        static_cast<unsigned long long>(kp),
        static_cast<unsigned long long>(ku));

    log::write(log::Level::Info, "input", "input self-test complete");
}

} // namespace notyvos::input