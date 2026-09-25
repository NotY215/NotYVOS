#include <kernel/arch/x86_64/cpuid.hpp>

namespace notyvos::arch::x86_64
{

namespace
{
void copy_cstr(char* dst, const char* src, usize max) noexcept
{
    usize i = 0;
    while (i + 1 < max && src[i])
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}
} // namespace

CpuInfo detect_cpu_info() noexcept
{
    CpuInfo info{};

    // Leaf 0: vendor + max basic leaf
    const auto l0 = cpuid(0);
    info.max_basic_leaf = l0.eax;

    // Vendor string: 12 bytes from EBX, EDX, ECX (in that order)
    char vendor[13];
    auto store4 = [](char* p, u32 v)
    {
        p[0] = static_cast<char>(v & 0xFF);
        p[1] = static_cast<char>((v >> 8) & 0xFF);
        p[2] = static_cast<char>((v >> 16) & 0xFF);
        p[3] = static_cast<char>((v >> 24) & 0xFF);
    };
    store4(vendor + 0, l0.ebx);
    store4(vendor + 4, l0.edx);
    store4(vendor + 8, l0.ecx);
    vendor[12] = '\0';
    copy_cstr(info.vendor, vendor, sizeof(info.vendor));

    // Leaf 1: family/model/stepping + feature bits
    if (info.max_basic_leaf >= 1)
    {
        const auto l1 = cpuid(1);
        const u32 base_family = (l1.eax >> 8) & 0xF;
        const u32 base_model = (l1.eax >> 4) & 0xF;
        const u32 ext_family = (l1.eax >> 20) & 0xFF;
        const u32 ext_model = (l1.eax >> 16) & 0xF;
        info.stepping = l1.eax & 0xF;
        info.family = (base_family == 0xF) ? (base_family + ext_family) : base_family;
        info.model = (base_family == 0x6 || base_family == 0xF) ? ((ext_model << 4) | base_model)
                                                                : base_model;

        info.has_fpu = (l1.edx >> 0) & 1;
        info.has_msr = (l1.edx >> 5) & 1;
        info.has_apic = (l1.edx >> 9) & 1;
        info.has_sse = (l1.edx >> 25) & 1;
        info.has_sse2 = (l1.edx >> 26) & 1;
        info.has_sse3 = (l1.ecx >> 0) & 1;
        info.has_ssse3 = (l1.ecx >> 9) & 1;
        info.has_sse41 = (l1.ecx >> 19) & 1;
        info.has_sse42 = (l1.ecx >> 20) & 1;
        info.has_x2apic = (l1.ecx >> 21) & 1;
        info.has_avx = (l1.ecx >> 28) & 1;
        info.apic_id = (l1.ebx >> 24) & 0xFF;
    }

    // Leaf 7 subleaf 0: AVX2, 1GB pages
    if (info.max_basic_leaf >= 7)
    {
        const auto l7 = cpuid(7, 0);
        info.has_avx2 = (l7.ebx >> 5) & 1;
        info.has_1gpages = (l7.edx >> 26) & 1;
    }

    // Extended leaf 0x80000000: max extended leaf
    const auto le = cpuid(0x80000000);
    info.max_extended_leaf = le.eax;

    // Leaf 0x80000001: long mode, NX
    if (info.max_extended_leaf >= 0x80000001)
    {
        const auto l1e = cpuid(0x80000001);
        info.has_longmode = (l1e.edx >> 29) & 1;
        info.has_nx = (l1e.edx >> 20) & 1;
        info.has_rdtscp = (l1e.edx >> 27) & 1;
    }

    // Extended brand string: leaves 0x80000002..0x80000004 (48 bytes)
    if (info.max_extended_leaf >= 0x80000004)
    {
        char brand[49];
        for (u32 i = 0; i < 3; ++i)
        {
            const auto b = cpuid(0x80000002 + i);
            store4(brand + i * 16 + 0, b.eax);
            store4(brand + i * 16 + 4, b.ebx);
            store4(brand + i * 16 + 8, b.ecx);
            store4(brand + i * 16 + 12, b.edx);
        }
        brand[48] = '\0';

        // Trim leading spaces (common on Intel).
        const char* p = brand;
        while (*p == ' ')
            ++p;
        copy_cstr(info.brand, p, sizeof(info.brand));
    }
    else
    {
        copy_cstr(info.brand, "(unknown)", sizeof(info.brand));
    }

    return info;
}

} // namespace notyvos::arch::x86_64
