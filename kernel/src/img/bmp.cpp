#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{

namespace
{

struct BmpFileHeader
{
    u16 magic;
    u32 file_size;
    u16 r0, r1;
    u32 pix_off;
} __attribute__((packed));

struct BmpInfoHeader
{
    u32 hdr_size;
    i32 width;
    i32 height;
    u16 planes;
    u16 bpp;
    u32 compression;
    u32 image_size;
    i32 xppm, yppm;
    u32 used_colors;
    u32 important_colors;
} __attribute__((packed));

inline u16 rd16(const u8* p) noexcept
{
    return static_cast<u16>(p[0] | (p[1] << 8));
}
inline u32 rd32(const u8* p) noexcept
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

} // namespace

bool decode_bmp_impl(const void* data, usize size, Image& out) noexcept
{
    if (size < 54)
        return false;
    const auto* b = static_cast<const u8*>(data);

    const u32 pix_off = rd32(b + 10);
    const u32 hdr_sz = rd32(b + 14);
    if (hdr_sz < 40)
        return false;

    const i32 w = static_cast<i32>(rd32(b + 18));
    const i32 h = static_cast<i32>(rd32(b + 22));
    const u16 bpp = rd16(b + 28);
    const u32 cmp = rd32(b + 30);

    if (w <= 0 || h == 0)
        return false;
    if (cmp != 0)
        return false; // BI_RGB only
    if (bpp != 24 && bpp != 32)
        return false;

    const bool top_down = (h < 0);
    const u32 uw = static_cast<u32>(w);
    const u32 uh = static_cast<u32>(top_down ? -h : h);

    if (uw > 4096 || uh > 4096)
        return false;

    const u32 row_stride = ((uw * bpp + 31u) / 32u) * 4u;
    const u64 needed = static_cast<u64>(pix_off) + static_cast<u64>(row_stride) * uh;
    if (needed > size)
        return false;

    const usize n_pixels = static_cast<usize>(uw) * uh;
    auto* pix = static_cast<u32*>(mm::Heap::allocate(n_pixels * sizeof(u32)));
    if (!pix)
        return false;

    for (u32 y = 0; y < uh; ++y)
    {
        const u32 src_y = top_down ? y : (uh - 1u - y);
        const u8* row = b + pix_off + src_y * row_stride;
        for (u32 x = 0; x < uw; ++x)
        {
            const u8* p = row + x * (bpp / 8u);
            const u32 rr = p[2];
            const u32 gg = p[1];
            const u32 bb = p[0];
            pix[y * uw + x] = (rr << 16) | (gg << 8) | bb;
        }
    }

    out.width = uw;
    out.height = uh;
    out.pixels = pix;
    out.owned = true;
    return true;
}

} // namespace notyvos::img
