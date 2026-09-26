#include <kernel/libk/mem.hpp>
#include <kernel/sched/scheduler.hpp>
#include <kernel/syscall/uaccess.hpp>

namespace notyvos::syscall
{

bool user_range_ok(uptr addr, usize len) noexcept
{
    if (len == 0)
        return true;
    const uptr end = addr + len;
    if (end < addr)
        return false; // overflow
    auto* cur = sched::scheduler_current();
    if (!cur)
        return false;
    if (cur->user_hi == 0)
        return false; // kernel task; no user VA
    return addr >= cur->user_lo && end <= cur->user_hi;
}

bool copy_from_user(void* kdst, uptr uaddr, usize n) noexcept
{
    if (!user_range_ok(uaddr, n))
        return false;
    libk::memcpy(kdst, reinterpret_cast<const void*>(uaddr), n);
    return true;
}

bool copy_to_user(uptr uaddr, const void* ksrc, usize n) noexcept
{
    if (!user_range_ok(uaddr, n))
        return false;
    libk::memcpy(reinterpret_cast<void*>(uaddr), ksrc, n);
    return true;
}

} // namespace notyvos::syscall
