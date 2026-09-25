#include <kernel/arch/x86_64/gdt.hpp>
#include <kernel/arch/x86_64/tss.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

extern "C" void notyvos_tss_load(u16 selector) noexcept;

namespace
{
alignas(16) Tss g_tss = {};
}

TssDescriptorInfo tss_descriptor() noexcept
{
    TssDescriptorInfo info{};
    info.base = reinterpret_cast<u64>(&g_tss);
    info.limit = sizeof(Tss) - 1;
    return info;
}

void tss_init() noexcept
{
    g_tss.iomap_base = sizeof(Tss);
    // IST entries are set by tss_set_ist before LTR is invoked.
    notyvos_tss_load(gdt_sel::kTss);

    log::write(log::Level::Info, "tss", "loaded: base=0x%x limit=%u",
               static_cast<u64>(tss_descriptor().base), static_cast<u64>(tss_descriptor().limit));
}

void tss_set_rsp0(u64 rsp0) noexcept
{
    g_tss.rsp0 = rsp0;
}

void tss_set_ist(u32 index, u64 rsp_top) noexcept
{
    if (index < 1 || index > 7)
        return;
    g_tss.ist[index - 1] = rsp_top;
}

} // namespace notyvos::arch::x86_64
