#pragma once
#include <kernel/bt/bt.hpp>

namespace notyvos::bt::hci
{

// HCI packet indicators (USB transport).
constexpr u8 kPktCommand    = 0x01;
constexpr u8 kPktAcl        = 0x02;
constexpr u8 kPktSco        = 0x03;
constexpr u8 kPktEvent      = 0x04;

// OGF / OCF for the commands we use.
constexpr u16 kOgfLinkControl   = 0x01;
constexpr u16 kOgfControllerBase = 0x03;
constexpr u16 kOgfInformational  = 0x04;
constexpr u16 kOgfLeController   = 0x08;

constexpr u16 kOcfReset              = 0x0003;
constexpr u16 kOcfReadBdAddr         = 0x0009;
constexpr u16 kOcfSetEventMask       = 0x0001;
constexpr u16 kOcfSetEventMaskPage2  = 0x0063;
constexpr u16 kOcfWriteLocalName     = 0x0013;
constexpr u16 kOcfInquiry            = 0x0001;
constexpr u16 kOcfInquiryCancel      = 0x0002;
constexpr u16 kOcfCreateConn         = 0x0005;
constexpr u16 kOcfDisconnect         = 0x0006;
constexpr u16 kOcfLinkKeyReply       = 0x000B;
constexpr u16 kOcfPinCodeReply       = 0x000D;
constexpr u16 kOcfReadLocalVersion   = 0x0001;

// Event codes.
constexpr u8 kEvtInquiryComplete    = 0x01;
constexpr u8 kEvtInquiryResult      = 0x02;
constexpr u8 kEvtConnComplete       = 0x03;
constexpr u8 kEvtDisconnComplete    = 0x05;
constexpr u8 kEvtCmdComplete        = 0x0E;
constexpr u8 kEvtCmdStatus          = 0x0F;
constexpr u8 kEvtPinCodeRequest     = 0x16;
constexpr u8 kEvtLinkKeyRequest     = 0x17;
constexpr u8 kEvtLinkKeyNotif       = 0x18;

struct Command
{
    u16 opcode;
    u8  plen;
    u8  params[255];
} __attribute__((packed));

struct Event
{
    u8  code;
    u8  plen;
    u8  params[255];
} __attribute__((packed));

// Send an HCI command to the transport.
bool send_command(u16 opcode, const u8* params, u8 plen) noexcept;

// Called by bt::hci_receive when the packet type is HCI event.
void handle_event(const u8* event, usize len) noexcept;

// Called by bt::hci_receive when the packet type is ACL data.
void handle_acl(const u8* data, usize len) noexcept;

} // namespace notyvos::bt::hci