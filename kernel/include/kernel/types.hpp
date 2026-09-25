#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

namespace notyvos {

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using usize = size_t;
using isize = ptrdiff_t;

using uptr = uintptr_t;
using iptr = intptr_t;

static_assert(sizeof(u8)  == 1, "u8 must be 1 byte");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
static_assert(sizeof(usize) == 8, "NOTYVOS is x86-64 only at this phase");

[[noreturn]] inline void halt_forever() noexcept {
    for (;;) {
        asm volatile("cli; hlt" ::: "memory");
    }
}

} // namespace notyvos