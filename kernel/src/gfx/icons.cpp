#include <kernel/fs/vfs.hpp>
#include <kernel/gfx/hal.hpp>
#include <kernel/gfx/icons.hpp>
#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::gfx::icons
{

namespace
{

constexpr u32 kNativeSize = 64;

struct Slot
{
    u32* pixels;
    u32 size;
};

Slot g_slots[static_cast<u32>(Id::Count)] = {};

const char* path_for(Id id) noexcept
{
    switch (id)
    {
    case Id::Explorer:
        return "/icons/explorer.ico";
    case Id::Settings:
        return "/icons/settings.ico";
    case Id::Terminal:
        return "/icons/terminal.ico";
    case Id::GameLauncher:
        return "/icons/game-launcher.ico";
    case Id::Bin:
        return "/icons/bin.ico";
    case Id::Profile:
        return "/icons/profile-picture.ico";
    case Id::StartButton:
        return "/icons/startbutton.ico";
    default:
        return nullptr;
    }
}

bool load_one(Id id) noexcept
{
    const char* path = path_for(id);
    if (!path)
        return false;

    auto* vn = fs::vfs_lookup(path, "/");
    if (!vn)
    {
        log::write(log::Level::Warn, "icons", "%s: not found in VFS", path);
        return false;
    }
    if (!vn->ops || !vn->ops->size || !vn->ops->read)
    {
        log::write(log::Level::Warn, "icons", "%s: no read/size ops", path);
        return false;
    }

    const isize sz = vn->ops->size(vn);
    if (sz <= 0 || sz > 512 * 1024)
    {
        log::write(log::Level::Warn, "icons", "%s: bad size %lld", path,
                   static_cast<long long>(sz));
        return false;
    }

    auto* buf = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz)));
    if (!buf)
        return false;

    isize got = 0;
    while (got < sz)
    {
        const isize n =
            vn->ops->read(vn, buf + got, static_cast<usize>(got), static_cast<usize>(sz - got));
        if (n <= 0)
            break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(buf);
        return false;
    }

    img::Image decoded{};
    const bool ok = img::decode(buf, static_cast<usize>(sz), decoded);
    mm::Heap::deallocate(buf);
    if (!ok || !decoded.pixels || decoded.width == 0 || decoded.height == 0)
    {
        log::write(log::Level::Warn, "icons", "%s: ICO decode failed", path);
        img::free(decoded);
        return false;
    }

    // Resize to kNativeSize x kNativeSize (nearest-neighbour) so the
    // compositor sees a consistent bitmap size.
    const usize out_bytes = static_cast<usize>(kNativeSize) * kNativeSize * sizeof(u32);
    auto* out = static_cast<u32*>(mm::Heap::allocate(out_bytes));
    if (!out)
    {
        img::free(decoded);
        return false;
    }
    for (u32 j = 0; j < kNativeSize; ++j)
    {
        const u32 sy = (j * decoded.height) / kNativeSize;
        for (u32 i = 0; i < kNativeSize; ++i)
        {
            const u32 sx = (i * decoded.width) / kNativeSize;
            out[j * kNativeSize + i] = decoded.pixels[sy * decoded.width + sx];
        }
    }
    img::free(decoded);

    g_slots[static_cast<u32>(id)].pixels = out;
    g_slots[static_cast<u32>(id)].size = kNativeSize;
    return true;
}

} // namespace

void init() noexcept
{
    u32 loaded = 0;
    for (u32 i = 1; i < static_cast<u32>(Id::Count); ++i)
    {
        const char* p = path_for(static_cast<Id>(i));
        if (load_one(static_cast<Id>(i)))
        {
            ++loaded;
            log::write(log::Level::Debug, "icons", "loaded %s", p);
        }
        else
        {
            log::write(log::Level::Warn, "icons", "FAILED %s", p);
        }
    }
    log::write(log::Level::Info, "icons", "loaded %llu/%llu ICO icons from /icons",
               static_cast<unsigned long long>(loaded),
               static_cast<unsigned long long>(static_cast<u32>(Id::Count) - 1));
}

const u32* bitmap(Id id) noexcept
{
    const u32 i = static_cast<u32>(id);
    if (i >= static_cast<u32>(Id::Count))
        return nullptr;
    return g_slots[i].pixels;
}

u32 bitmap_size(Id id) noexcept
{
    const u32 i = static_cast<u32>(id);
    if (i >= static_cast<u32>(Id::Count))
        return 0;
    return g_slots[i].size;
}

void draw(Id id, i32 x, i32 y, u32 size) noexcept
{
    const u32* src = bitmap(id);
    if (!src || size == 0)
        return;
    const u32 src_size = bitmap_size(id);
    if (src_size == 0)
        return;

    HalSurface* tgt = hal_target();
    if (!tgt || !tgt->pixels)
        return;

    for (u32 j = 0; j < size; ++j)
    {
        const u32 sy = (j * src_size) / size;
        for (u32 i = 0; i < size; ++i)
        {
            const u32 sx = (i * src_size) / size;
            const u32 c = src[sy * src_size + sx];
            if ((c & 0xFFFFFFu) == 0)
                continue;
            const i32 px = x + static_cast<i32>(i);
            const i32 py = y + static_cast<i32>(j);
            if (px < 0 || py < 0)
                continue;
            if (static_cast<u32>(px) >= tgt->width)
                continue;
            if (static_cast<u32>(py) >= tgt->height)
                continue;
            tgt->pixels[static_cast<usize>(py) * tgt->pitch + static_cast<u32>(px)] = c;
        }
    }
}

} // namespace notyvos::gfx::icons
