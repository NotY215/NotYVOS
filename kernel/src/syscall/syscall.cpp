#include <kernel/arch/x86_64/msr.hpp>
#include <kernel/log.hpp>
#include <kernel/syscall/syscall.hpp>

namespace notyvos::syscall
{

void syscall_init() noexcept
{
    using namespace arch::x86_64;
    const u64 star = (static_cast<u64>(0x10) << 48) | (static_cast<u64>(0x08) << 32);
    wrmsr(msr::kIa32Star, star);
    wrmsr(msr::kIa32Lstar, reinterpret_cast<u64>(&syscall_entry));
    const u64 fmask =
        (1ULL << 9) | (1ULL << 8) | (1ULL << 10) | (1ULL << 18) | (1ULL << 14) | (1ULL << 16);
    wrmsr(msr::kIa32Fmask, fmask);
    const u64 efer = rdmsr(msr::kIa32Efer);
    wrmsr(msr::kIa32Efer, efer | msr::kEferSce);
    log::write(log::Level::Info, "syscall", "initialized: STAR=0x%llx LSTAR=0x%llx",
               static_cast<unsigned long long>(star),
               static_cast<unsigned long long>(reinterpret_cast<uptr>(&syscall_entry)));
}

} // namespace notyvos::syscall
