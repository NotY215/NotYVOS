#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/pic.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
constexpr u8 kIcw1 = 0x11;
constexpr u8 kIcw4 = 0x01; // 8086 mode
constexpr u8 kEoi = 0x20;
} // namespace

void pic_remap(u8 master_offset, u8 slave_offset) noexcept
{
    const u8 mask_master = inb(kPicMasterData);
    const u8 mask_slave = inb(kPicSlaveData);

    outb(kPicMasterCmd, kIcw1);
    io_wait();
    outb(kPicSlaveCmd, kIcw1);
    io_wait();

    outb(kPicMasterData, master_offset);
    io_wait();
    outb(kPicSlaveData, slave_offset);
    io_wait();

    outb(kPicMasterData, 0x04); // slave on IRQ2
    io_wait();
    outb(kPicSlaveData, 0x02);
    io_wait();

    outb(kPicMasterData, kIcw4);
    io_wait();
    outb(kPicSlaveData, kIcw4);
    io_wait();

    outb(kPicMasterData, mask_master);
    outb(kPicSlaveData, mask_slave);

    log::write(log::Level::Info, "pic", "remapped: master=%u slave=%u",
               static_cast<u64>(master_offset), static_cast<u64>(slave_offset));
}

void pic_send_eoi(u8 irq) noexcept
{
    if (irq >= 8)
        outb(kPicSlaveCmd, kEoi);
    outb(kPicMasterCmd, kEoi);
}

void pic_set_mask(u8 irq, bool masked) noexcept
{
    const u16 port = (irq < 8) ? kPicMasterData : kPicSlaveData;
    const u8 bit = static_cast<u8>(irq & 7);
    u8 cur = inb(port);
    if (masked)
        cur = static_cast<u8>(cur | (1u << bit));
    else
        cur = static_cast<u8>(cur & ~(1u << bit));
    outb(port, cur);
}

void pic_disable_all() noexcept
{
    outb(kPicMasterData, 0xFF);
    outb(kPicSlaveData, 0xFF);
}

} // namespace notyvos::arch::x86_64
