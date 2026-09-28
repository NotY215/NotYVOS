#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/arch/x86_64/tss.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/sched/scheduler.hpp>

namespace notyvos::sched
{

extern "C" void notyvos_switch_context(u64* save_slot, u64 new_rsp);

namespace
{
Task* g_head = nullptr;
Task* g_tail = nullptr;
u32 g_task_count = 0;
Task g_boot_task{};
} // namespace

void scheduler_init() noexcept
{
    g_boot_task.tid = 0;
    g_boot_task.state = TaskState::Running;
    const char* n = "boot";
    for (u32 i = 0; n[i] && i < kTaskNameMax - 1; ++i)
        g_boot_task.name[i] = n[i];
    g_boot_task.name[kTaskNameMax - 1] = '\0';
    auto* me = arch::x86_64::this_cpu();
    me->current_task = &g_boot_task;
}

void scheduler_add(Task* t)
{
    if (!t)
        return;
    t->next = nullptr;
    if (!g_head)
    {
        g_head = g_tail = t;
    }
    else
    {
        g_tail->next = t;
        g_tail = t;
    }
    ++g_task_count;
}

Task* scheduler_current() noexcept
{
    return arch::x86_64::this_cpu()->current_task;
}
void scheduler_set_current(Task* t) noexcept
{
    arch::x86_64::this_cpu()->current_task = t;
}
u64 scheduler_task_count() noexcept
{
    return g_task_count;
}

static Task* pick_next() noexcept
{
    if (!g_head)
        return nullptr;
    Task* start = g_head;
    Task* t = start;
    do
    {
        if (t->state == TaskState::Ready)
            return t;
        t = t->next ? t->next : g_head;
    } while (t != start);
    return nullptr;
}

static void remove_from_queue(Task* t)
{
    if (!t || !g_head)
        return;
    if (g_head == g_tail)
    {
        g_head = g_tail = nullptr;
    }
    else if (t == g_head)
    {
        g_head = g_head->next;
        g_tail->next = g_head;
    }
    else if (t == g_tail)
    {
        Task* p = g_head;
        while (p->next != g_tail)
            p = p->next;
        g_tail = p;
        g_tail->next = g_head;
    }
    else
    {
        Task* p = g_head;
        while (p->next != t)
            p = p->next;
        p->next = t->next;
    }
    if (g_task_count)
        --g_task_count;
}

static void switch_to(Task* next)
{
    auto* me = arch::x86_64::this_cpu();
    Task* prev = me->current_task;
    if (prev == next)
        return;

    // Only demote Running -> Ready. A Zombie stays Zombie so the parent
    // can find it in sys_wait.
    if (prev && prev->state == TaskState::Running)
    {
        prev->state = TaskState::Ready;
    }
    next->state = TaskState::Running;
    me->current_task = next;

    if (next->kernel_stack)
    {
        const u64 top = reinterpret_cast<u64>(next->kernel_stack) + next->kernel_stack_size;
        arch::x86_64::tss_set_rsp0(top);
        arch::x86_64::percpu_set_kernel_stack(me->index, top);
    }

    if (next->cr3 && prev && next->cr3 != prev->cr3)
    {
        asm volatile("mov %0, %%cr3" ::"r"(next->cr3) : "memory");
    }

    notyvos_switch_context(&prev->kernel_rsp, next->kernel_rsp);
}

void scheduler_start() noexcept
{
    Task* next = pick_next();
    if (!next)
    {
        log::write(log::Level::Warn, "sched", "no runnable task; idling");
        for (;;)
            asm volatile("hlt");
    }
    switch_to(next);
}

void scheduler_tick() noexcept
{
    Task* next = pick_next();
    if (!next)
        return;
    if (next == scheduler_current())
        return;
    switch_to(next);
}

void scheduler_yield() noexcept
{
    Task* next = pick_next();
    if (!next || next == scheduler_current())
        return;
    switch_to(next);
}

void scheduler_exit_current(int code)
{
    Task* me = scheduler_current();
    log::write(log::Level::Info, "sched", "task '%s' (tid=%u) exited code=%d", me ? me->name : "?",
               static_cast<unsigned long long>(me ? me->tid : 0), code);
    if (me && me != &g_boot_task)
    {
        me->exit_code = code;
        me->state = TaskState::Zombie;
        remove_from_queue(me);
    }
    Task* next = pick_next();
    if (!next)
    {
        log::write(log::Level::Warn, "sched", "no next task; idling");
        for (;;)
            asm volatile("hlt");
    }
    switch_to(next);
    for (;;)
        asm volatile("hlt");
}

} // namespace notyvos::sched
