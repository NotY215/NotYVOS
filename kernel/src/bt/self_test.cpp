#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/bt/bt.hpp>
#include <kernel/bt/hci.hpp>
#include <kernel/bt/self_test.hpp>

namespace notyvos::bt
{

void self_test() noexcept
{
    log::write(log::Level::Info, "bt", "self-test: transport=%u devices=%llu",
               static_cast<u64>(current_transport()),
               static_cast<unsigned long long>(device_count()));

    const bool ok = (current_transport() != Transport::None);
    log::write(ok ? log::Level::Info : log::Level::Warn, "bt",
               "Bluetooth self-test: %s", ok ? "PASS" : "SKIP (no transport)");
}

} // namespace notyvos::bt