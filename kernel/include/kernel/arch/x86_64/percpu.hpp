#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

constexpr u32 kMaxCpus = 64;

struct CpuLocal
{
    u32 index; // 0..kMaxCpus-1
    u32 lapic_id;
    u32 processor_id;
    u64 kernel_stack_top; // virtual
    // Fields for later: current_thread, irq_depth, etc.
};

CpuLocal* this_cpu() noexcept;
CpuLocal* cpu_by_index(u32 i) noexcept;
CpuLocal* cpu_by_lapic(u32 id) noexcept;

void percpu_init_bsp() noexcept; // called by BSP after heap init
void percpu_register(u32 index, u32 lapic_id, u32 processor_id, u64 stack_top) noexcept;

u32 cpu_count() noexcept;

} // namespace notyvos::arch::x86_64
