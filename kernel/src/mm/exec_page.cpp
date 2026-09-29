#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/exec_page.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::mm
{

namespace
{
constexpr uptr kArenaBase = 0xffffffffD0000000ULL;
constexpr usize kArenaSize = 64ULL * 1024 * 1024;
constexpr usize kArenaPages = kArenaSize / static_cast<usize>(kPageSize);

uptr g_cursor = 0;
bool g_ready = false;
} // namespace

void ExecArena::init() noexcept
{
    constexpr u64 kFlags = page_flags::Present | page_flags::Writable;
    for (usize i = 0; i < kArenaPages; ++i)
    {
        const uptr phys = PhysicalMemory::allocate_frame();
        if (!phys)
        {
            log::write(log::Level::Error, "exec", "arena OOM at page %llu",
                       static_cast<unsigned long long>(i));
            return;
        }
        if (!VirtualMemory::map_page(kArenaBase + i * static_cast<uptr>(kPageSize), phys, kFlags))
        {
            log::write(log::Level::Error, "exec", "arena map fail page %llu",
                       static_cast<unsigned long long>(i));
            return;
        }
    }
    g_cursor = 0;
    g_ready = true;
    log::write(log::Level::Info, "exec", "arena %llu MB at 0x%llx",
               static_cast<unsigned long long>(kArenaSize / (1024 * 1024)),
               static_cast<unsigned long long>(kArenaBase));
}

void* ExecArena::publish(const void* code, usize size) noexcept
{
    if (!g_ready || !code || size == 0)
        return nullptr;
    const usize aligned = (size + 15) & ~static_cast<usize>(15);
    if (g_cursor + aligned > kArenaSize)
    {
        log::write(log::Level::Error, "exec", "arena full");
        return nullptr;
    }
    auto* dst = reinterpret_cast<u8*>(kArenaBase + g_cursor);
    libk::memcpy(dst, code, size);
    g_cursor += aligned;
    return dst;
}

usize ExecArena::used_bytes() noexcept
{
    return g_cursor;
}
usize ExecArena::capacity_bytes() noexcept
{
    return kArenaSize;
}

} // namespace notyvos::mm
