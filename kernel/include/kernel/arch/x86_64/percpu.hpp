#pragma once
#include <kernel/types.hpp>

namespace notyvos
{
namespace sched
{
struct Task;
}
} // namespace notyvos

namespace notyvos::arch::x86_64
{

constexpr u32 kMaxCpus = 64;

struct CpuLocal
{
    u64 syscall_user_rsp;               // offset  0
    u64 syscall_kernel_rsp;             // offset  8
    u64 kernel_stack_top;               // offset 16
    u32 index;                          // offset 24
    u32 lapic_id;                       // offset 28
    u32 processor_id;                   // offset 32
    u32 _pad;                           // offset 36
    notyvos::sched::Task* current_task; // offset 40
};

CpuLocal* this_cpu() noexcept;
CpuLocal* cpu_by_index(u32 i) noexcept;
CpuLocal* cpu_by_lapic(u32 id) noexcept;

void percpu_init_bsp() noexcept;
void percpu_register(u32 index, u32 lapic_id, u32 processor_id, u64 stack_top) noexcept;
void percpu_set_kernel_stack(u32 index, u64 stack_top) noexcept;
void percpu_set_gs(CpuLocal* cpu) noexcept;

u32 cpu_count() noexcept;

} // namespace notyvos::arch::x86_64
