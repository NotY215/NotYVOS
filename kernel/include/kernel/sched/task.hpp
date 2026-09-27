#pragma once
#include <kernel/fs/file.hpp>
#include <kernel/types.hpp>

namespace notyvos::sched
{

enum class TaskState : u8
{
    Unused = 0,
    Ready,
    Running,
    Zombie
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

    fs::FileTable* files;
    char cwd[256];

    // Process hierarchy.
    Task* parent;
    Task* first_child;
    Task* next_sibling;
    i32 exit_code;
    bool reaped;

    Task* next; // run-queue link
};

Task* task_create_kernel(const char* name, TaskEntryFn fn, void* arg, usize stack_size) noexcept;
Task* task_create_user(const char* name, uptr entry, uptr user_rsp, uptr cr3, uptr user_lo,
                       uptr user_hi) noexcept;

// Creates a child process that resumes execution in user mode at the state
// captured in `parent_frame_copy`. The `frame_copy` buffer must remain valid
// for the child's lifetime (typically a heap allocation freed by the child
// after it returns to user mode).
Task* task_create_forked(const char* name, uptr cr3, uptr user_lo, uptr user_hi, void* frame_copy,
                         usize frame_size) noexcept;

void task_destroy(Task* t) noexcept;
Task* task_by_tid(u32 tid) noexcept;

void task_add_child(Task* parent, Task* child) noexcept;
void task_remove_child(Task* parent, Task* child) noexcept;

} // namespace notyvos::sched
