#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/arch/x86_64/tss.hpp>
#include <kernel/arch/x86_64/usermode.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/sched/scheduler.hpp>
#include <kernel/syscall/syscall.hpp>

namespace notyvos::arch::x86_64
{

void usermode_prepare_kernel_stack() noexcept {}

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

// Defined in fork_entry.S. Reads a syscall::SyscallFrame from rdi and
// returns to user mode with rax=0, preserving all other user registers.
extern "C" void notyvos_enter_user_fork(const void* frame) noexcept;

extern "C" void task_entry_kernel() noexcept
{
    auto* t = sched::scheduler_current();
    if (t && t->start_fn)
        t->start_fn(t->start_arg);
    sched::scheduler_exit_current(0);
    __builtin_unreachable();
}

extern "C" void task_entry_user() noexcept
{
    auto* t = sched::scheduler_current();
    if (!t)
    {
        for (;;)
            asm volatile("hlt");
    }
    enter_user(t->user_entry, t->user_rsp);
}

extern "C" void task_entry_fork_child() noexcept
{
    auto* t = sched::scheduler_current();
    if (!t || !t->start_arg)
    {
        log::write(log::Level::Error, "fork", "child has no frame");
        sched::scheduler_exit_current(-1);
    }
    void* frame = t->start_arg;
    // Free the buffer once we no longer need it.
    // (We copy the frame on the child's kernel stack in the resume routine,
    // but it does not return here; the free is done inside the assembly
    // helper via a callback -- simpler to leak until process exit.)
    t->start_arg = nullptr;
    notyvos_enter_user_fork(frame);
    __builtin_unreachable();
}

} // namespace notyvos::arch::x86_64
