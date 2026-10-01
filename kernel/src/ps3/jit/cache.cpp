#include <kernel/log.hpp>
#include <kernel/ps3/jit/cache.hpp>

namespace notyvos::ps3::jit
{

namespace
{
constexpr u32 kBuckets = 1024;
constexpr u32 kBucketMask = kBuckets - 1;

Block* g_buckets[kBuckets] = {};
u64 g_hits = 0;
u64 g_misses = 0;
u64 g_blocks = 0;
u64 g_bytes = 0;

inline u32 bucket_of(u64 pc) noexcept
{
    // PC is always 4-byte aligned. Drop the two zero bits, then mix.
    u64 h = pc >> 2;
    h ^= h >> 13;
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= h >> 29;
    return static_cast<u32>(h) & kBucketMask;
}
} // namespace

void TranslationCache::init() noexcept
{
    for (u32 i = 0; i < kBuckets; ++i)
        g_buckets[i] = nullptr;
    g_hits = 0;
    g_misses = 0;
    g_blocks = 0;
    g_bytes = 0;
    log::write(log::Level::Info, "jit-cache", "init %u buckets",
               static_cast<unsigned long long>(kBuckets));
}

Block* TranslationCache::lookup(u64 ppc_pc) noexcept
{
    Block* b = g_buckets[bucket_of(ppc_pc)];
    while (b)
    {
        if (b->ppc_start == ppc_pc)
        {
            ++g_hits;
            return b;
        }
        b = b->next;
    }
    ++g_misses;
    return nullptr;
}

void TranslationCache::insert(Block* blk) noexcept
{
    if (!blk)
        return;
    const u32 idx = bucket_of(blk->ppc_start);
    blk->next = g_buckets[idx];
    g_buckets[idx] = blk;
    ++g_blocks;
    g_bytes += blk->x86_size;
}

void TranslationCache::flush() noexcept
{
    for (u32 i = 0; i < kBuckets; ++i)
        g_buckets[i] = nullptr;
    g_blocks = 0;
    g_bytes = 0;
}

Block* TranslationCache::probe(u64 ppc_pc) noexcept
{
    Block* b = g_buckets[bucket_of(ppc_pc)];
    while (b)
    {
        if (b->ppc_start == ppc_pc)
            return b;
        b = b->next;
    }
    return nullptr;
}

void TranslationCache::invalidate_range(u64 lo, u64 hi) noexcept
{
    if (lo >= hi)
        return;
    u64 removed = 0;
    for (u32 i = 0; i < kBuckets; ++i)
    {
        Block** pp = &g_buckets[i];
        while (*pp)
        {
            Block* b = *pp;
            if (b->ppc_start >= lo && b->ppc_start < hi)
            {
                *pp = b->next;
                if (g_blocks > 0)
                    --g_blocks;
                if (g_bytes >= b->x86_size)
                    g_bytes -= b->x86_size;
                ++removed;
            }
            else
            {
                pp = &b->next;
            }
        }
    }
    (void)removed;
}

u64 TranslationCache::hits() noexcept
{
    return g_hits;
}
u64 TranslationCache::misses() noexcept
{
    return g_misses;
}
u64 TranslationCache::block_count() noexcept
{
    return g_blocks;
}
u64 TranslationCache::bytes_used() noexcept
{
    return g_bytes;
}

} // namespace notyvos::ps3::jit
