#include <kernel/log.hpp>
#include <kernel/mm/pmm.hpp>

namespace notyvos::mm
{

namespace
{

constexpr u64 kFrameSize = 0x1000;
constexpr u64 kMaxFrames = 1ULL << 22;        // 4M frames = 16 GB
constexpr u64 kBitmapWords = kMaxFrames / 64; // 512 KB

alignas(8) u64 g_bitmap[kBitmapWords];
u64 g_total_frames = 0;
u64 g_free_frames = 0;
u64 g_hhdm_offset = 0;

inline bool bit_test(u64 idx) noexcept
{
    return (g_bitmap[idx / 64] & (1ULL << (idx % 64))) != 0;
}
inline void bit_set(u64 idx) noexcept
{
    g_bitmap[idx / 64] |= (1ULL << (idx % 64));
}
inline void bit_clear(u64 idx) noexcept
{
    g_bitmap[idx / 64] &= ~(1ULL << (idx % 64));
}

} // namespace

void PhysicalMemory::init(const limine_memmap_response* memmap, u64 hhdm_offset) noexcept
{
    g_hhdm_offset = hhdm_offset;

    for (u64 i = 0; i < kBitmapWords; ++i)
        g_bitmap[i] = ~0ULL;

    // Highest end of any USABLE region. Anything beyond does not exist for
    // allocation purposes, even if the memmap lists reserved MMIO up there.
    u64 highest_usable_end = 0;
    u64 usable_bytes = 0;
    for (usize i = 0; i < memmap->entry_count; ++i)
    {
        const auto* e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;
        const u64 end = e->base + e->length;
        if (end > highest_usable_end)
            highest_usable_end = end;
        usable_bytes += e->length;
    }

    g_total_frames = (highest_usable_end + kFrameSize - 1) / kFrameSize;
    if (g_total_frames > kMaxFrames)
        g_total_frames = kMaxFrames;

    // Free all usable frames.
    u64 freed = 0;
    for (usize i = 0; i < memmap->entry_count; ++i)
    {
        const auto* e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        const u64 start = (e->base + kFrameSize - 1) & ~(kFrameSize - 1);
        const u64 end = (e->base + e->length) & ~(kFrameSize - 1);

        for (u64 addr = start; addr < end; addr += kFrameSize)
        {
            const u64 idx = addr / kFrameSize;
            if (idx >= g_total_frames)
                break;
            if (bit_test(idx))
            {
                bit_clear(idx);
                ++freed;
            }
        }
    }

    // Reserve low 4 MB (kernel image, boot structs, our bitmap itself).
    for (u64 addr = 0; addr < 0x400000; addr += kFrameSize)
    {
        const u64 idx = addr / kFrameSize;
        if (idx >= g_total_frames)
            break;
        if (!bit_test(idx))
        {
            bit_set(idx);
            if (freed > 0)
                --freed;
        }
    }

    g_free_frames = freed;

    log::write(log::Level::Info, "pmm", "usable %u MB, total %u frames, free %u frames",
               usable_bytes / (1024 * 1024), g_total_frames, g_free_frames);
}

uptr PhysicalMemory::allocate_frame() noexcept
{
    const u64 words = (g_total_frames + 63) / 64;
    for (u64 w = 0; w < words; ++w)
    {
        if (g_bitmap[w] == ~0ULL)
            continue;
        const u64 free_bits = ~g_bitmap[w];
        const u32 bit = static_cast<u32>(__builtin_ctzll(free_bits));
        const u64 idx = w * 64 + bit;
        if (idx >= g_total_frames)
            return 0;
        bit_set(idx);
        --g_free_frames;
        return static_cast<uptr>(idx * kFrameSize);
    }
    return 0;
}

void PhysicalMemory::free_frame(uptr phys) noexcept
{
    const u64 idx = static_cast<u64>(phys) / kFrameSize;
    if (idx >= g_total_frames)
        return;
    if (bit_test(idx))
    {
        bit_clear(idx);
        ++g_free_frames;
    }
}

uptr PhysicalMemory::allocate_contiguous(usize count) noexcept
{
    if (count == 0)
        return 0;
    const u64 words = (g_total_frames + 63) / 64;
    u64 run = 0;
    for (u64 w = 0; w < words; ++w)
    {
        for (u32 b = 0; b < 64; ++b)
        {
            const u64 idx = w * 64 + b;
            if (idx >= g_total_frames)
                return 0;
            if (bit_test(idx))
            {
                run = 0;
                continue;
            }
            if (++run == count)
            {
                const u64 base = (idx + 1 - count) * kFrameSize;
                for (u64 k = 0; k < count; ++k)
                    bit_set((idx + 1 - count) + k);
                g_free_frames -= count;
                return static_cast<uptr>(base);
            }
        }
    }
    return 0;
}

u64 PhysicalMemory::total_bytes() noexcept
{
    return g_total_frames * kFrameSize;
}
u64 PhysicalMemory::free_bytes() noexcept
{
    return g_free_frames * kFrameSize;
}
u64 PhysicalMemory::used_bytes() noexcept
{
    return (g_total_frames - g_free_frames) * kFrameSize;
}
u64 PhysicalMemory::total_frames() noexcept
{
    return g_total_frames;
}
u64 PhysicalMemory::free_frames() noexcept
{
    return g_free_frames;
}

} // namespace notyvos::mm
