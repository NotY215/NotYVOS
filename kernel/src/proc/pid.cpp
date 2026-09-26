#include <kernel/proc/pid.hpp>

namespace notyvos::proc
{

namespace
{
constexpr u32 kMaxPid = 4096;
constexpr u32 kWords = kMaxPid / 64;
u64 g_bitmap[kWords] = {};
u32 g_next = 1;
} // namespace

u32 alloc_pid() noexcept
{
    for (u32 n = 0; n < kMaxPid; ++n)
    {
        const u32 pid = 1 + ((g_next + n) % (kMaxPid - 1));
        const u32 w = pid / 64, b = pid % 64;
        if ((g_bitmap[w] & (1ULL << b)) == 0)
        {
            g_bitmap[w] |= (1ULL << b);
            g_next = pid + 1;
            return pid;
        }
    }
    return 0;
}

void free_pid(u32 pid) noexcept
{
    if (pid == 0 || pid >= kMaxPid)
        return;
    g_bitmap[pid / 64] &= ~(1ULL << (pid % 64));
}

} // namespace notyvos::proc
