#include "libnoty.h"

/* Simple bump allocator on top of sys_brk. No free-list; free() is a no-op.
 * This is fine while user programs are small and short-lived. */

static u64 g_heap_cur = 0;
static u64 g_heap_end = 0;

static u64 align_up(u64 x, u64 a)
{
    return (x + a - 1) & ~(a - 1);
}

void* malloc(u64 size)
{
    if (size == 0)
        return 0;
    size = align_up(size, 16);

    if (g_heap_cur == 0)
    {
        g_heap_cur = (u64)sys_brk(0);
        g_heap_end = g_heap_cur;
    }

    if (g_heap_cur + size > g_heap_end)
    {
        const u64 want = g_heap_cur + size;
        const u64 got = (u64)sys_brk(want);
        if (got < want)
            return 0;
        g_heap_end = got;
    }

    void* p = (void*)g_heap_cur;
    g_heap_cur += size;
    return p;
}

void free(void* p)
{
    (void)p;
}

void* calloc(u64 count, u64 size)
{
    const u64 total = count * size;
    void* p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void* realloc(void* p, u64 new_size)
{
    void* q = malloc(new_size);
    if (!q)
        return 0;
    if (p)
        memcpy(q, p, new_size); /* over-copies; the caller knows the old size */
    return q;
}
