#pragma once
#include <kernel/types.hpp>

namespace notyvos::proc
{
bool clone_user_range(uptr src_cr3, uptr dst_cr3, uptr lo, uptr hi) noexcept;
}
