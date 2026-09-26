#pragma once
#include <kernel/types.hpp>

namespace notyvos::syscall
{

// Validate a user pointer range against the current task's [user_lo, user_hi).
// Returns true if the whole range is inside and the caller may dereference
// it via the identity mapping used by the user program.
bool user_range_ok(uptr addr, usize len) noexcept;

// Copy `n` bytes from user memory `uaddr` to kernel `kdst`.
bool copy_from_user(void* kdst, uptr uaddr, usize n) noexcept;

// Copy `n` bytes from kernel `ksrc` to user memory `uaddr`.
bool copy_to_user(uptr uaddr, const void* ksrc, usize n) noexcept;

} // namespace notyvos::syscall
