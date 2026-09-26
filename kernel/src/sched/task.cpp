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

extern "C" void task_entry_kernel();
extern "C" void task_entry_user();

namespace
{
Task* g_tasks[256] = {};
}

// ---------------------------------------------------------------------------
// Initial stack layout for a new task.
//
// notyvos_switch_context ends with:
//   popfq
//   pop r15
//   pop r14
//   pop r13
//   pop r12
//   pop rbx
//   pop rbp
//   ret
//
// So the incoming stack must hold, at increasing addresses:
//   [rsp+0]  rflags
//   [rsp+8]  r15
//   [rsp+16] r14
//   [rsp+24] r13
//   [rsp+32] r12
//   [rsp+40] rbx
//   [rsp+48] rbp
//   [rsp+56] return target
//
// Since *--sp writes to decreasing addresses, the writes must be in the
// REVERSE of the pop order. Do not reorder this block.
// ---------------------------------------------------------------------------
static void build_initial_stack(Task* t, u64 entry) noexcept
{
    const uptr top = reinterpret_cast<uptr>(t->kernel_stack) + t->kernel_stack_size;
    u64* sp = reinterpret_cast<u64*>(top & ~static_cast<uptr>(15));

    *--sp = entry;    // [rsp+56] ret target
    *--sp = 0;        // [rsp+48] rbp
    *--sp = 0;        // [rsp+40] rbx
    *--sp = 0;        // [rsp+32] r12
    *--sp = 0;        // [rsp+24] r13
    *--sp = 0;        // [rsp+16] r14
    *--sp = 0;        // [rsp+8]  r15
    *--sp = 0x202ULL; // [rsp+0]  rflags (IF=1, reserved bit 1 set)

    t->kernel_rsp = reinterpret_cast<u64>(sp);
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
    t->name[kTaskNameMax - 1] = '\0';
    t->start_fn = fn;
    t->start_arg = arg;
    t->kernel_stack_size = stack_size;
    t->kernel_stack = static_cast<u8*>(mm::Heap::allocate_aligned(stack_size, 16));
    t->cr3 = 0;

    build_initial_stack(t, reinterpret_cast<u64>(&task_entry_kernel));

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
    t->name[kTaskNameMax - 1] = '\0';
    t->user_entry = entry;
    t->user_rsp = user_rsp;
    t->cr3 = cr3;
    t->user_lo = user_lo;
    t->user_hi = user_hi;
    t->kernel_stack_size = 16384;
    t->kernel_stack = static_cast<u8*>(mm::Heap::allocate_aligned(t->kernel_stack_size, 16));

    build_initial_stack(t, reinterpret_cast<u64>(&task_entry_user));

    register_task(t);
    return t;
}

void task_destroy(Task* t) noexcept
{
    if (!t)
        return;
    if (t->tid < 256)
        g_tasks[t->tid] = nullptr;
    if (t->kernel_stack)
        mm::Heap::deallocate(t->kernel_stack);
    mm::Heap::deallocate(t);
}

} // namespace notyvos::sched
