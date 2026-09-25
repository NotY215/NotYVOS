#pragma once
#include <kernel/types.hpp>

namespace notyvos::mm
{

class Heap
{
public:
    static void init() noexcept;

    static void* allocate(usize size) noexcept;
    static void* allocate_aligned(usize size, usize align) noexcept;
    static void deallocate(void* ptr) noexcept;

    static usize used_bytes() noexcept;
    static usize total_bytes() noexcept;
};

} // namespace notyvos::mm

// Standard C++ allocation operators.
// `operator new` / `operator new[]` must match the implicit non-noexcept
// signature; `operator delete` is standardly `noexcept`.
void* operator new(notyvos::usize size);
void* operator new[](notyvos::usize size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete(void* ptr, notyvos::usize) noexcept;
void operator delete[](void* ptr, notyvos::usize) noexcept;
