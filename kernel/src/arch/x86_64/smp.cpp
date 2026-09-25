#include <kernel/arch/x86_64/lapic.hpp>
#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/arch/x86_64/smp.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::arch::x86_64
{

// Defined in smp_entry.S.
extern "C" void ap_entry(limine_mp_info* cpu) noexcept;

// Defined in percpu.cpp. Declared here so ap_main can call it.
extern "C" void notyvos_set_gs(u64 base) noexcept;

namespace
{
constexpr u32 kApStackSize = 16384;
u32 g_started = 0;
} // namespace

u32 smp_started_count() noexcept
{
    return g_started;
}

void smp_init(limine_mp_response* response) noexcept
{
    if (!response)
    {
        log::write(log::Level::Warn, "smp", "no Limine MP response, single-CPU mode");
        g_started = 1;
        return;
    }

    const u32 bsp_lapic = response->bsp_lapic_id;
    const u64 total = response->cpu_count;

    log::write(log::Level::Info, "smp", "bsp_lapic=%llu total_cpus=%llu",
               static_cast<unsigned long long>(bsp_lapic), static_cast<unsigned long long>(total));

    percpu_register(0, bsp_lapic, 0, 0);

    u32 next_index = 1;
    for (u64 i = 0; i < total; ++i)
    {
        auto* cpu = response->cpus[i];
        if (!cpu)
            continue;
        if (cpu->lapic_id == bsp_lapic)
            continue;
        if (next_index >= kMaxCpus)
            break;

        void* stack = mm::Heap::allocate_aligned(kApStackSize, 16);
        if (!stack)
        {
            log::write(log::Level::Error, "smp", "AP %llu stack alloc failed",
                       static_cast<unsigned long long>(i));
            continue;
        }
        const u64 stack_top = reinterpret_cast<u64>(stack) + kApStackSize;

        percpu_register(next_index, cpu->lapic_id, cpu->processor_id, stack_top);

        cpu->extra_argument = stack_top;
        cpu->goto_address = &ap_entry;

        log::write(log::Level::Info, "smp", "started AP index=%llu lapic=%llu proc=%llu",
                   static_cast<unsigned long long>(next_index),
                   static_cast<unsigned long long>(cpu->lapic_id),
                   static_cast<unsigned long long>(cpu->processor_id));
        ++next_index;
    }

    g_started = next_index;
    log::write(log::Level::Info, "smp", "total started: %llu (incl. BSP)",
               static_cast<unsigned long long>(next_index));
}

extern "C" void ap_main(limine_mp_info* cpu) noexcept
{
    Lapic::enable_for_ap();

    const auto* me = cpu_by_lapic(cpu->lapic_id);
    if (me)
    {
        notyvos_set_gs(reinterpret_cast<u64>(const_cast<CpuLocal*>(me)));
        log::write(log::Level::Info, "ap", "index=%llu lapic=%llu online",
                   static_cast<unsigned long long>(me->index),
                   static_cast<unsigned long long>(me->lapic_id));
    }
}

} // namespace notyvos::arch::x86_64
