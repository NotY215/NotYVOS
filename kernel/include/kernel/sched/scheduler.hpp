#pragma once
#include <kernel/sched/task.hpp>

namespace notyvos::sched
{

void scheduler_init() noexcept;
void scheduler_start() noexcept;
void scheduler_tick() noexcept;
void scheduler_yield() noexcept;
[[noreturn]] void scheduler_exit_current(int code);

void scheduler_add(Task* t);
Task* scheduler_current() noexcept;
void scheduler_set_current(Task* t) noexcept;
u64 scheduler_task_count() noexcept;

} // namespace notyvos::sched
