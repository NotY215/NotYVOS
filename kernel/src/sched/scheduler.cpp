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
Task* g_head = nullptr; // circular run queue
Task* g_tail = nullptr;
u32 g_task_count = 0;
Task g_boot_task{}; // kernel_main itself
} // namespace

void scheduler_init() noexcept
{
    // Register kernel_main as the boot task.
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
    // Round-robin: advance tail->next to head; return head.
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

static void remove(Task* t)
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

    if (prev)
        prev->state = TaskState::Ready;
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
    (void)code;
    Task* me = scheduler_current();
    log::write(log::Level::Info, "sched", "task '%s' (tid=%u) exited", me ? me->name : "?",
               static_cast<unsigned long long>(me ? me->tid : 0));
    if (me && me != &g_boot_task)
    {
        me->state = TaskState::Zombie;
        remove(me);
        // leak the Task struct for now; task_destroy would free the stack
        // we are standing on.
    }
    Task* next = pick_next();
    if (!next)
    {
        log::write(log::Level::Warn, "sched", "no next task; idling");
        for (;;)
            asm volatile("hlt");
    }
    switch_to(next);
    // switch_to never returns for the zombie.
    for (;;)
        asm volatile("hlt");
}

} // namespace notyvos::sched
