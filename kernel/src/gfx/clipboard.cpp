#include <kernel/gfx/clipboard.hpp>
#include <kernel/libk/string.hpp>

namespace notyvos::gfx::clipboard
{

namespace
{
Content g_buf = { Kind::Empty, {0}, false };
u64     g_writes = 0;
}

void set_text(const char* text) noexcept
{
    g_buf.kind = Kind::Text;
    g_buf.cut  = false;
    usize i = 0;
    if (text)
    {
        while (text[i] && i < kMaxClipText - 1) { g_buf.text[i] = text[i]; ++i; }
    }
    g_buf.text[i] = 0;
    ++g_writes;
}

void set_file(const char* path, bool cut) noexcept
{
    g_buf.kind = Kind::File;
    g_buf.cut  = cut;
    usize i = 0;
    if (path)
    {
        while (path[i] && i < kMaxClipText - 1) { g_buf.text[i] = path[i]; ++i; }
    }
    g_buf.text[i] = 0;
    ++g_writes;
}

const Content& get() noexcept { return g_buf; }
bool has() noexcept { return g_buf.kind != Kind::Empty; }
void clear() noexcept { g_buf.kind = Kind::Empty; g_buf.text[0] = 0; }
u64  writes() noexcept { return g_writes; }

} // namespace notyvos::gfx::clipboard