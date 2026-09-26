#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
alignas(64) CpuLocal g_cpus[kMaxCpus] = {};
u32 g_cpu_count = 0;
} // namespace

CpuLocal* this_cpu() noexcept
{
    return reinterpret_cast<CpuLocal*>(rdmsr(msr::kIa32GsBase));
}
CpuLocal* cpu_by_index(u32 i) noexcept
{
    return (i < kMaxCpus) ? &g_cpus[i] : nullptr;
}
CpuLocal* cpu_by_lapic(u32 id) noexcept
{
    for (u32 i = 0; i < g_cpu_count; ++i)
        if (g_cpus[i].lapic_id == id)
            return &g_cpus[i];
    return nullptr;
}
u32 cpu_count() noexcept
{
    return g_cpu_count;
}

void percpu_register(u32 index, u32 lapic_id, u32 processor_id, u64 stack_top) noexcept
{
    if (index >= kMaxCpus)
        return;
    g_cpus[index].index = index;
    g_cpus[index].lapic_id = lapic_id;
    g_cpus[index].processor_id = processor_id;
    g_cpus[index].kernel_stack_top = stack_top;
    g_cpus[index].syscall_kernel_rsp = stack_top;
    if (index + 1 > g_cpu_count)
        g_cpu_count = index + 1;
}

void percpu_set_kernel_stack(u32 index, u64 stack_top) noexcept
{
    if (index >= kMaxCpus)
        return;
    g_cpus[index].kernel_stack_top = stack_top;
    g_cpus[index].syscall_kernel_rsp = stack_top;
}

void percpu_set_gs(CpuLocal* cpu) noexcept
{
    wrmsr(msr::kIa32GsBase, reinterpret_cast<u64>(cpu));
}

void percpu_init_bsp() noexcept
{
    percpu_register(0, 0, 0, 0);
    percpu_set_gs(&g_cpus[0]);
    log::write(log::Level::Info, "percpu", "bsp gs_base=0x%llx",
               static_cast<unsigned long long>(reinterpret_cast<uptr>(&g_cpus[0])));
}

extern "C" void notyvos_set_gs(u64 base) noexcept
{
    wrmsr(msr::kIa32GsBase, base);
}

} // namespace notyvos::arch::x86_64
