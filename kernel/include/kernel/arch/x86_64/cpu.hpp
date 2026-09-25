#pragma once

#include <kernel/arch/x86_64/cpuid.hpp>

namespace notyvos::arch::x86_64
{

// Called once early in kernel_main, after serial + console + log are ready.
void cpu_init() noexcept;

// Exposed for other subsystems that need feature flags.
const CpuInfo& cpu_info() noexcept;

} // namespace notyvos::arch::x86_64
