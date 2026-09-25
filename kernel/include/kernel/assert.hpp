#pragma once

#include <kernel/panic.hpp>

#define KASSERT(expr)                                                                              \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            ::notyvos::panic("KASSERT failed: %s at %s:%d", #expr, __FILE__, __LINE__);            \
        }                                                                                          \
    } while (0)

#define KASSERT_MSG(expr, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            ::notyvos::panic("KASSERT failed: %s (%s) at %s:%d", #expr, (msg), __FILE__,           \
                             __LINE__);                                                            \
        }                                                                                          \
    } while (0)

#define UNREACHABLE() ::notyvos::panic("unreachable at %s:%d", __FILE__, __LINE__)
