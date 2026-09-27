#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/proc/pid.hpp>
#include <kernel/sched/task.hpp>

namespace notyvos::sched
{

extern "C" void task_entry_kernel() noexcept;
extern "C" void task_entry_user() noexcept;
extern "C" void task_entry_fork_child() noexcept;

namespace
{
Task* g_tasks[256] = {};
}

static void build_initial_stack(Task* t, u64 entry) noexcept
{
    const uptr top = reinterpret_cast<uptr>(t->kernel_stack) + t->kernel_stack_size;
    u64* sp = reinterpret_cast<u64*>(top & ~static_cast<uptr>(15));
    *--sp = entry;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0x202ULL;
    t->kernel_rsp = reinterpret_cast<u64>(sp);
}

static void init_task_common(Task* t)
{
    t->files = fs::filetable_create();
    t->cwd[0] = '/';
    t->cwd[1] = 0;
}

Task* task_by_tid(u32 tid) noexcept
{
    return (tid < 256) ? g_tasks[tid] : nullptr;
}
static void register_task(Task* t)
{
    if (t->tid < 256)
        g_tasks[t->tid] = t;
}

Task* task_create_kernel(const char* name, TaskEntryFn fn, void* arg, usize stack_size) noexcept
{
    void* raw = mm::Heap::allocate(sizeof(Task));
    if (!raw)
        return nullptr;
    auto* t = static_cast<Task*>(raw);
    libk::memset(t, 0, sizeof(Task));
    t->tid = proc::alloc_pid();
    t->state = TaskState::Ready;
    libk::strncpy(t->name, name ? name : "task", kTaskNameMax - 1);
    t->start_fn = fn;
    t->start_arg = arg;
    t->kernel_stack_size = stack_size;
    t->kernel_stack = static_cast<u8*>(mm::Heap::allocate_aligned(stack_size, 16));
    build_initial_stack(t, reinterpret_cast<u64>(&task_entry_kernel));
    init_task_common(t);
    t->brk_start = 0;
    t->brk_current = 0;
    t->brk_max = 0;
    register_task(t);
    return t;
}

Task* task_create_user(const char* name, uptr entry, uptr user_rsp, uptr cr3, uptr user_lo,
                       uptr user_hi) noexcept
{
    void* raw = mm::Heap::allocate(sizeof(Task));
    if (!raw)
        return nullptr;
    auto* t = static_cast<Task*>(raw);
    libk::memset(t, 0, sizeof(Task));
    t->tid = proc::alloc_pid();
    t->state = TaskState::Ready;
    libk::strncpy(t->name, name ? name : "user", kTaskNameMax - 1);
    t->user_entry = entry;
    t->user_rsp = user_rsp;
    t->cr3 = cr3;
    t->user_lo = user_lo;
    t->user_hi = user_hi;
    t->kernel_stack_size = 16384;
    t->kernel_stack = static_cast<u8*>(mm::Heap::allocate_aligned(t->kernel_stack_size, 16));
    build_initial_stack(t, reinterpret_cast<u64>(&task_entry_user));
    init_task_common(t);
    t->brk_start = 0;
    t->brk_current = 0;
    t->brk_max = 0;
    register_task(t);
    return t;
}

Task* task_create_forked(const char* name, uptr cr3, uptr user_lo, uptr user_hi, void* frame_copy,
                         usize frame_size) noexcept
{
    void* raw = mm::Heap::allocate(sizeof(Task));
    if (!raw)
        return nullptr;
    auto* t = static_cast<Task*>(raw);
    libk::memset(t, 0, sizeof(Task));
    t->tid = proc::alloc_pid();
    t->state = TaskState::Ready;
    libk::strncpy(t->name, name ? name : "forked", kTaskNameMax - 1);
    t->cr3 = cr3;
    t->user_lo = user_lo;
    t->user_hi = user_hi;
    t->kernel_stack_size = 16384;
    t->kernel_stack = static_cast<u8*>(mm::Heap::allocate_aligned(t->kernel_stack_size, 16));

    // Store the copied frame at start_arg. It will be consumed by
    // task_entry_fork_child() and freed there.
    void* frame_heap = mm::Heap::allocate(frame_size);
    libk::memcpy(frame_heap, frame_copy, frame_size);
    t->start_arg = frame_heap;

    build_initial_stack(t, reinterpret_cast<u64>(&task_entry_fork_child));
    init_task_common(t);
    register_task(t);
    return t;
}

void task_destroy(Task* t) noexcept
{
    if (!t)
        return;
    if (t->tid < 256)
        g_tasks[t->tid] = nullptr;
    if (t->files)
        fs::filetable_destroy(t->files);
    if (t->start_arg && t->state == TaskState::Zombie)
    {
        // fork-frame buffer is only owned by fork children; freeing
        // blindly could double-free non-fork tasks. Guard by name prefix.
        if (t->name[0] == 'f' && t->name[1] == 'o')
        {
            mm::Heap::deallocate(t->start_arg);
        }
    }
    if (t->kernel_stack)
        mm::Heap::deallocate(t->kernel_stack);
    mm::Heap::deallocate(t);
}

void task_add_child(Task* parent, Task* child) noexcept
{
    if (!parent || !child)
        return;
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

void task_remove_child(Task* parent, Task* child) noexcept
{
    if (!parent || !child)
        return;
    Task** pp = &parent->first_child;
    while (*pp)
    {
        if (*pp == child)
        {
            *pp = child->next_sibling;
            child->next_sibling = nullptr;
            return;
        }
        pp = &(*pp)->next_sibling;
    }
}

} // namespace notyvos::sched
