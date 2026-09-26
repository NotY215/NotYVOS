#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/log.hpp>
#include <kernel/sched/scheduler.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/uaccess.hpp>

namespace notyvos::syscall
{

namespace
{

i64 sys_write(u64 /*fd*/, u64 buf, u64 len) noexcept
{
    if (len > 4096)
        return -1;
    char tmp[256];
    u64 done = 0;
    while (done < len)
    {
        const u64 chunk = (len - done > sizeof(tmp)) ? sizeof(tmp) : (len - done);
        if (!copy_from_user(tmp, buf + done, static_cast<usize>(chunk)))
            return -1;
        for (u64 i = 0; i < chunk; ++i)
        {
            arch::x86_64::SerialPort::write_char(tmp[i]);
            if (fb::Console::ready())
                fb::Console::put(tmp[i]);
        }
        done += chunk;
    }
    return static_cast<i64>(len);
}

i64 sys_yield() noexcept
{
    sched::scheduler_yield();
    return 0;
}

i64 sys_getpid() noexcept
{
    auto* cur = sched::scheduler_current();
    return cur ? static_cast<i64>(cur->tid) : 0;
}

} // namespace

extern "C" void syscall_dispatch(SyscallFrame* f) noexcept
{
    switch (f->rax)
    {
    case nr::kExit:
        log::write(log::Level::Info, "user", "sys_exit(%llu) from tid=%llu",
                   static_cast<unsigned long long>(f->rdi),
                   static_cast<unsigned long long>(
                       sched::scheduler_current() ? sched::scheduler_current()->tid : 0));
        sched::scheduler_exit_current(static_cast<int>(f->rdi));
        break;
    case nr::kWrite:
        f->rax = static_cast<u64>(sys_write(f->rdi, f->rsi, f->rdx));
        break;
    case nr::kYield:
        f->rax = static_cast<u64>(sys_yield());
        break;
    case nr::kGetPid:
        f->rax = static_cast<u64>(sys_getpid());
        break;
    default:
        log::write(log::Level::Warn, "syscall", "unknown nr=%llu rip=0x%llx",
                   static_cast<unsigned long long>(f->rax),
                   static_cast<unsigned long long>(f->rip));
        f->rax = static_cast<u64>(-1);
        break;
    }
}

} // namespace notyvos::syscall
