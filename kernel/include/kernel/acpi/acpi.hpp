#pragma once
#include <kernel/types.hpp>

namespace notyvos::acpi
{

// `limine_rsdp_addr` is whatever Limine put in rsdp_response->address. It may
// be either a physical address or an HHDM-mapped pointer depending on the
// Limine version. The implementation detects which and handles both.
bool init(u64 hhdm_offset, void* limine_rsdp_addr) noexcept;

bool power_available() noexcept;

[[noreturn]] void power_off() noexcept;
[[noreturn]] void restart() noexcept;

} // namespace notyvos::acpi
