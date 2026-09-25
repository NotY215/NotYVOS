#include <kernel/arch/x86_64/cpu.hpp>
#include <kernel/arch/x86_64/gdt.hpp>
#include <kernel/arch/x86_64/idt.hpp>
#include <kernel/arch/x86_64/pic.hpp>
#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/arch/x86_64/tss.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
CpuInfo g_info{};

// IST stacks. 16 KB each is generous for Phase 1B.
alignas(16) u8 g_df_stack[16384];
alignas(16) u8 g_nmi_stack[16384];

struct FeatureLine
{
    const char* name;
    bool enabled;
};

void log_features(const CpuInfo& info) noexcept
{
    const FeatureLine flags[] = {
        {"fpu", info.has_fpu},           {"msr", info.has_msr},         {"apic", info.has_apic},
        {"x2apic", info.has_x2apic},     {"sse", info.has_sse},         {"sse2", info.has_sse2},
        {"sse3", info.has_sse3},         {"ssse3", info.has_ssse3},     {"sse4.1", info.has_sse41},
        {"sse4.2", info.has_sse42},      {"avx", info.has_avx},         {"avx2", info.has_avx2},
        {"rdtscp", info.has_rdtscp},     {"1gpages", info.has_1gpages}, {"nx", info.has_nx},
        {"longmode", info.has_longmode},
    };
    char buf[160];
    usize n = 0;
    for (const auto& f : flags)
    {
        if (!f.enabled)
            continue;
        if (n > 0 && n < sizeof(buf) - 2)
            buf[n++] = ' ';
        for (const char* p = f.name; *p && n < sizeof(buf) - 2; ++p)
            buf[n++] = *p;
    }
    buf[n] = '\0';
    log::write(log::Level::Info, "cpu", "features: %s", buf);
}
} // namespace

const CpuInfo& cpu_info() noexcept
{
    return g_info;
}

void cpu_init() noexcept
{
    g_info = detect_cpu_info();

    log::write(log::Level::Info, "cpu", "vendor: %s", g_info.vendor);
    log::write(log::Level::Info, "cpu", "brand:  %s", g_info.brand);
    log::write(log::Level::Info, "cpu", "family %u model %u stepping %u apic_id %u",
               static_cast<u64>(g_info.family), static_cast<u64>(g_info.model),
               static_cast<u64>(g_info.stepping), static_cast<u64>(g_info.apic_id));

    gdt_init();

    // IST stacks must be installed before LTR picks up the TSS descriptor.
    const u64 df_top = reinterpret_cast<u64>(g_df_stack) + sizeof(g_df_stack);
    const u64 nmi_top = reinterpret_cast<u64>(g_nmi_stack) + sizeof(g_nmi_stack);
    tss_set_ist(1, df_top);
    tss_set_ist(2, nmi_top);

    tss_init();

    idt_init();

    // Remap PIC, disable all IRQs, then unmask IRQ0 (timer).
    pic_remap(32, 40);
    pic_disable_all();
    pic_set_mask(0, false);

    pit_init(100);

    log_features(g_info);
}

} // namespace notyvos::arch::x86_64
