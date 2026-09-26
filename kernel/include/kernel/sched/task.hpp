#pragma once
#include <kernel/types.hpp>

namespace notyvos::sched
{

enum class TaskState : u8
{
    Unused = 0,
    Ready,
    Running,
    Zombie,
};

using TaskEntryFn = void (*)(void*);

constexpr u32 kTaskNameMax = 16;

struct Task
{
    u32 tid;
    TaskState state;
    char name[kTaskNameMax];
    u64 kernel_rsp;
    u8* kernel_stack;
    usize kernel_stack_size;
    uptr cr3;
    uptr user_entry;
    uptr user_rsp;
    TaskEntryFn start_fn;
    void* start_arg;
    uptr user_lo;
    uptr user_hi;
    Task* next;
};

Task* task_create_kernel(const char* name, TaskEntryFn fn, void* arg, usize stack_size) noexcept;
Task* task_create_user(const char* name, uptr entry, uptr user_rsp, uptr cr3, uptr user_lo,
                       uptr user_hi) noexcept;
void task_destroy(Task* t) noexcept;
Task* task_by_tid(u32 tid) noexcept;

} // namespace notyvos::sched
