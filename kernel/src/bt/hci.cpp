#include <kernel/bt/bt.hpp>
#include <kernel/bt/hci.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::bt
{
void on_inquiry_result(const Address& addr, u8 dev_class_major, i8 rssi) noexcept;
void on_connection_complete(const Address& addr, bool ok) noexcept;
} // namespace notyvos::bt

extern "C" bool bt_transport_send(const notyvos::u8* data, notyvos::usize len) noexcept;

namespace notyvos::bt::hci
{

namespace
{
u16 g_last_opcode = 0;
u8 g_last_status = 0;
u64 g_commands_sent = 0;
u64 g_events_received = 0;

void handle_cmd_complete(const u8* p, usize len) noexcept
{
    if (len < 4)
        return;
    g_last_status = p[3];
    (void)g_last_opcode;
}

void handle_inquiry_result(const u8* p, usize len) noexcept
{
    if (len < 1)
        return;
    const u8 n = p[0];
    usize off = 1;
    for (u8 i = 0; i < n; ++i)
    {
        if (off + 14 > len)
            break;
        Address addr{};
        libk::memcpy(addr.b, p + off, 6);
        const u8 dev_class_major = static_cast<u8>((p[off + 9] >> 2) & 0x1F);
        (void)p[off + 10];
        (void)p[off + 11];
        const i8 rssi = static_cast<i8>(p[off + 13]);
        on_inquiry_result(addr, dev_class_major, rssi);
        off += 14;
    }
}

void handle_conn_complete(const u8* p, usize len) noexcept
{
    if (len < 11)
        return;
    const u8 status = p[0];
    Address addr{};
    libk::memcpy(addr.b, p + 1, 6);
    on_connection_complete(addr, status == 0);
}

} // namespace

bool send_command(u16 opcode, const u8* params, u8 plen) noexcept
{
    u8 buf[3 + 255];
    buf[0] = kPktCommand;
    buf[1] = static_cast<u8>(opcode & 0xFF);
    buf[2] = static_cast<u8>((opcode >> 8) & 0xFF);
    buf[3] = plen;
    if (params && plen > 0)
        libk::memcpy(buf + 4, params, plen);

    if (!bt_transport_send(buf, 4u + plen))
        return false;
    g_last_opcode = opcode;
    ++g_commands_sent;
    return true;
}

void handle_event(const u8* event, usize len) noexcept
{
    if (!event || len < 2)
        return;
    ++g_events_received;
    const u8 code = event[0];
    const u8 plen = event[1];
    const u8* params = event + 2;
    if (2u + plen > len)
        return;

    switch (code)
    {
    case kEvtCmdComplete:
        handle_cmd_complete(params, plen);
        break;
    case kEvtInquiryResult:
        handle_inquiry_result(params, plen);
        break;
    case kEvtInquiryComplete:
        bt::stop_inquiry();
        break;
    case kEvtConnComplete:
        handle_conn_complete(params, plen);
        break;
    case kEvtDisconnComplete:
        if (plen >= 7)
        {
            Address addr{};
            libk::memcpy(addr.b, params + 1, 6);
            on_connection_complete(addr, false);
        }
        break;
    default:
        break;
    }
}

void handle_acl(const u8* data, usize len) noexcept
{
    if (len < 4)
        return;
    const u16 handle_flags = static_cast<u16>(data[0] | (data[1] << 8));
    const u16 dlen = static_cast<u16>(data[2] | (data[3] << 8));
    (void)handle_flags;
    (void)dlen;
}

} // namespace notyvos::bt::hci
