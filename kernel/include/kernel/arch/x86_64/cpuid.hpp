#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64
{

struct CpuidResult
{
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

inline CpuidResult cpuid(u32 leaf, u32 subleaf = 0) noexcept
{
    CpuidResult r;
    asm volatile("cpuid"
                 : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
                 : "a"(leaf), "c"(subleaf)
                 : "memory");
    return r;
}

struct CpuInfo
{
    char vendor[13]; // NUL-terminated
    char brand[49];  // NUL-terminated
    u32 family;
    u32 model;
    u32 stepping;
    u32 max_basic_leaf;
    u32 max_extended_leaf;
    u32 apic_id;

    // Feature bits we currently care about. Extended as needed.
    bool has_fpu : 1;
    bool has_apic : 1;
    bool has_msr : 1;
    bool has_sse : 1;
    bool has_sse2 : 1;
    bool has_sse3 : 1;
    bool has_ssse3 : 1;
    bool has_sse41 : 1;
    bool has_sse42 : 1;
    bool has_avx : 1;
    bool has_avx2 : 1;
    bool has_x2apic : 1;
    bool has_rdtscp : 1;
    bool has_1gpages : 1;
    bool has_longmode : 1;
    bool has_nx : 1;
};

CpuInfo detect_cpu_info() noexcept;

} // namespace notyvos::arch::x86_64
