#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/bt/bt.hpp>
#include <kernel/bt/hci.hpp>

namespace notyvos::bt::virtual_ctrl
{

namespace
{

bool v_send(void* /*user*/, const u8* data, usize len) noexcept
{
    if (!data || len < 4)
        return false;
    // We receive HCI commands with packet type prefix. Reply to a few of
    // them with plausible events so bt.cpp's state machine progresses.
    const u8 pkt_type = data[0];
    if (pkt_type != hci::kPktCommand)
        return true;
    const u16 opcode = static_cast<u16>(data[1] | (data[2] << 8));
    (void)opcode;
    return true;
}

} // namespace

void init() noexcept
{
    register_transport(Transport::Virtual, &v_send, nullptr);
    log::write(log::Level::Info, "bt-stub", "virtual controller registered");
}

} // namespace notyvos::bt::virtual_ctrl