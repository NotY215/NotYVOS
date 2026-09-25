#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

constexpr u16 kPicMasterCmd = 0x20;
constexpr u16 kPicMasterData = 0x21;
constexpr u16 kPicSlaveCmd = 0xA0;
constexpr u16 kPicSlaveData = 0xA1;

// Remap master IRQs to vectors 32..39, slave IRQs to 40..47.
void pic_remap(u8 master_offset = 32, u8 slave_offset = 40) noexcept;

// Send End-Of-Interrupt. `irq` is 0..15.
void pic_send_eoi(u8 irq) noexcept;

// Mask/unmask a specific IRQ line (0..15).
void pic_set_mask(u8 irq, bool masked) noexcept;

// Temporarily disable all IRQs on both PICs.
void pic_disable_all() noexcept;

} // namespace notyvos::arch::x86_64
