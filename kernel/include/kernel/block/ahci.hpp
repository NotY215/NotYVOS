#pragma once
#include <kernel/block/block.hpp>

namespace notyvos::block
{

// Scan PCI for AHCI controllers and register any attached SATA disks.
void ahci_init() noexcept;

} // namespace notyvos::block
