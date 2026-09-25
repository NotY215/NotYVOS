#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::mm
{

namespace
{

// NOTE: kPageSize is defined in paging.hpp and is not redefined here.
constexpr usize kHeapSize = 16 * 1024 * 1024;       // 16 MB
constexpr usize kHeapPages = kHeapSize / kPageSize; // 4096 pages
constexpr usize kHeaderSize = 16;                   // 8 used + 8 pad
constexpr uptr kHeapVirt = 0xffffffffC0000000ULL;   // kernel VA base

alignas(8) u64 g_used[kHeapPages / 64]; // 64 words
uptr g_virt_base = 0;
usize g_used_bytes = 0;

inline bool bit_test(usize i) noexcept
{
    return (g_used[i / 64] & (1ULL << (i % 64))) != 0;
}
inline void bit_set(usize i) noexcept
{
    g_used[i / 64] |= (1ULL << (i % 64));
}
inline void bit_clear(usize i) noexcept
{
    g_used[i / 64] &= ~(1ULL << (i % 64));
}

usize find_run(usize n, usize from = 0) noexcept
{
    if (n == 0 || n > kHeapPages)
        return static_cast<usize>(-1);
    usize run = 0;
    for (usize i = from; i < kHeapPages; ++i)
    {
        if (bit_test(i))
        {
            run = 0;
            continue;
        }
        if (++run == n)
            return i + 1 - n;
    }
    return static_cast<usize>(-1);
}

} // namespace

void Heap::init() noexcept
{
    const usize pages = kHeapPages;
    for (usize i = 0; i < pages; ++i)
    {
        const uptr phys = PhysicalMemory::allocate_frame();
        if (!phys)
        {
            log::write(log::Level::Error, "heap", "PMM exhausted at page %llu",
                       static_cast<unsigned long long>(i));
            return;
        }
        const uptr virt = kHeapVirt + i * static_cast<uptr>(kPageSize);
        if (!VirtualMemory::map_page(virt, phys, page_flags::Present | page_flags::Writable))
        {
            log::write(log::Level::Error, "heap", "map failed at page %llu",
                       static_cast<unsigned long long>(i));
            PhysicalMemory::free_frame(phys);
            return;
        }
    }
    g_virt_base = kHeapVirt;
    for (usize i = 0; i < kHeapPages / 64; ++i)
        g_used[i] = 0;
    g_used_bytes = 0;

    log::write(log::Level::Info, "heap", "initialized %llu MB at 0x%llx",
               static_cast<unsigned long long>(kHeapSize / (1024 * 1024)),
               static_cast<unsigned long long>(kHeapVirt));
}

void* Heap::allocate(usize size) noexcept
{
    return allocate_aligned(size, kHeaderSize);
}

void* Heap::allocate_aligned(usize size, usize align) noexcept
{
    if (size == 0)
        return nullptr;
    const usize total = size + kHeaderSize;
    const usize pages_needed = (total + kPageSize - 1) / kPageSize;

    const usize start = find_run(pages_needed);
    if (start == static_cast<usize>(-1))
        return nullptr;

    for (usize j = 0; j < pages_needed; ++j)
        bit_set(start + j);

    const uptr base = g_virt_base + start * static_cast<uptr>(kPageSize);
    *reinterpret_cast<usize*>(base) = pages_needed;

    uptr user = base + kHeaderSize;
    if (align > kHeaderSize)
    {
        const uptr mask = align - 1;
        user = (user + mask) & ~static_cast<uptr>(mask);
    }
    g_used_bytes += pages_needed * kPageSize;
    return reinterpret_cast<void*>(user);
}

void Heap::deallocate(void* ptr) noexcept
{
    if (!ptr)
        return;
    const uptr user = reinterpret_cast<uptr>(ptr);
    const uptr base = user & ~static_cast<uptr>(kPageSize - 1);
    const usize pages = *reinterpret_cast<const usize*>(base);
    if (pages == 0 || pages > kHeapPages)
        return;
    const usize start = (base - g_virt_base) / kPageSize;
    if (start + pages > kHeapPages)
        return;
    for (usize j = 0; j < pages; ++j)
        bit_clear(start + j);
    if (g_used_bytes >= pages * kPageSize)
        g_used_bytes -= pages * kPageSize;
}

usize Heap::used_bytes() noexcept
{
    return g_used_bytes;
}
usize Heap::total_bytes() noexcept
{
    return kHeapSize;
}

} // namespace notyvos::mm

void* operator new(notyvos::usize size)
{
    return notyvos::mm::Heap::allocate(size);
}
void* operator new[](notyvos::usize size)
{
    return notyvos::mm::Heap::allocate(size);
}
void operator delete(void* ptr) noexcept
{
    notyvos::mm::Heap::deallocate(ptr);
}
void operator delete[](void* ptr) noexcept
{
    notyvos::mm::Heap::deallocate(ptr);
}
void operator delete(void* ptr, notyvos::usize) noexcept
{
    notyvos::mm::Heap::deallocate(ptr);
}
void operator delete[](void* ptr, notyvos::usize) noexcept
{
    notyvos::mm::Heap::deallocate(ptr);
}
