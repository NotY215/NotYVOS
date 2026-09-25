#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
CpuLocal g_cpus[kMaxCpus] = {};
u32 g_cpu_count = 0;
} // namespace

CpuLocal* this_cpu() noexcept
{
    // Read GS base. We set it at init to point at our own CpuLocal.
    const u64 gs = rdmsr(msr::kIa32GsBase);
    return reinterpret_cast<CpuLocal*>(gs);
}

CpuLocal* cpu_by_index(u32 i) noexcept
{
    return (i < kMaxCpus) ? &g_cpus[i] : nullptr;
}

CpuLocal* cpu_by_lapic(u32 id) noexcept
{
    for (u32 i = 0; i < g_cpu_count; ++i)
    {
        if (g_cpus[i].lapic_id == id)
            return &g_cpus[i];
    }
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
    if (index + 1 > g_cpu_count)
        g_cpu_count = index + 1;
}

void percpu_init_bsp() noexcept
{
    percpu_register(0, 0 /* set by caller later */, 0, 0);
    // Point GS base at our own CpuLocal.
    wrmsr(msr::kIa32GsBase, reinterpret_cast<u64>(&g_cpus[0]));

    log::write(log::Level::Info, "percpu", "bsp gs_base=0x%llx",
               static_cast<u64>(reinterpret_cast<uptr>(&g_cpus[0])));
}
extern "C" void notyvos_set_gs(u64 base) noexcept
{
    using namespace notyvos::arch::x86_64;
    wrmsr(msr::kIa32GsBase, base);
}
} // namespace notyvos::arch::x86_64
