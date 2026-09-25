#pragma once
#include <kernel/types.hpp>
#include <limine.h>

namespace notyvos::arch::x86_64
{

// Limine v12 multiprocessor ("mp") protocol.
void smp_init(limine_mp_response* response) noexcept;

u32 smp_started_count() noexcept;

} // namespace notyvos::arch::x86_64
