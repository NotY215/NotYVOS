#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/arch/x86_64/tss.hpp>
#include <kernel/arch/x86_64/usermode.hpp>
#include <kernel/log.hpp>
#include <kernel/sched/scheduler.hpp>

namespace notyvos::arch::x86_64
{

void usermode_prepare_kernel_stack() noexcept
{
    // Reserved for phase 2B+; not used to launch init anymore.
}

[[noreturn]] void enter_user(uptr user_rip, uptr user_rsp) noexcept
{
    asm volatile("swapgs\n"
                 "mov $0x1B, %%ax\n"
                 "push %%rax\n"
                 "push %[rsp]\n"
                 "push $0x202\n"
                 "push $0x23\n"
                 "push %[rip]\n"
                 "iretq\n"
                 :
                 : [rip] "r"(user_rip), [rsp] "r"(user_rsp)
                 : "rax", "memory");
    __builtin_unreachable();
}

// Entered from the context switch when a kernel task starts.
extern "C" void task_entry_kernel() noexcept
{
    auto* t = sched::scheduler_current();
    if (t && t->start_fn)
        t->start_fn(t->start_arg);
    sched::scheduler_exit_current(0);
    __builtin_unreachable();
}

// Entered from the context switch when a user task starts.
extern "C" void task_entry_user() noexcept
{
    auto* t = sched::scheduler_current();
    if (!t)
    {
        log::write(log::Level::Error, "user", "task_entry_user with no current task");
        for (;;)
            asm volatile("hlt");
    }
    enter_user(t->user_entry, t->user_rsp);
}

} // namespace notyvos::arch::x86_64
