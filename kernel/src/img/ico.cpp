#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{
// Forward declaration — decode_png_impl lives in png.cpp.
bool decode_png_impl(const void* data, usize size, Image& out) noexcept;
}

namespace notyvos::img
{

namespace
{

inline u16 rd_le16(const u8* p) noexcept
{
    return static_cast<u16>(p[0] | (p[1] << 8));
}
inline u32 rd_le32(const u8* p) noexcept
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

} // namespace

// ICO is a container. We decode the first (usually largest) entry that
// is either an embedded PNG or a 32-bit BGRA DIB.
bool decode_ico_impl(const void* data, usize size, Image& out) noexcept
{
    if (size < 22)
        return false;
    const auto* b = static_cast<const u8*>(data);
    if (rd_le16(b) != 0 || rd_le16(b + 2) != 1)
        return false;

    const u16 count = rd_le16(b + 4);
    if (count == 0)
        return false;

    // First directory entry.
    const u8* de = b + 6;
    const u32 entry_size = rd_le32(de + 8);
    const u32 entry_offset = rd_le32(de + 12);
    if (entry_offset + entry_size > size)
        return false;

    const u8* payload = b + entry_offset;

    // PNG magic?
    static const u8 png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (entry_size >= 8 && libk::memcmp(payload, png_sig, 8) == 0)
        return decode_png_impl(payload, entry_size, out);

    // Otherwise treat as a 32-bit DIB (BITMAPINFOHEADER without the
    // 14-byte BITMAPFILEHEADER).
    if (entry_size < 40)
        return false;
    const u32 hdr_size = rd_le32(payload);
    if (hdr_size < 40)
        return false;
    const i32 w = static_cast<i32>(rd_le32(payload + 4));
    const i32 raw_h = static_cast<i32>(rd_le32(payload + 8));
    const u16 planes = rd_le16(payload + 12);
    const u16 bpp = rd_le16(payload + 14);
    if (w <= 0 || raw_h == 0 || planes != 1)
        return false;
    if (bpp != 32 && bpp != 24)
        return false;

    const u32 uw = static_cast<u32>(w);
    const u32 uh = static_cast<u32>(raw_h / 2); // DIB height is doubled (XOR + AND)
    if (uw > 512 || uh > 512)
        return false;

    const u32 row_stride = ((uw * static_cast<u32>(bpp) + 31u) / 32u) * 4u;
    const u64 needed = 40u + static_cast<u64>(row_stride) * uh;
    if (needed > entry_size)
        return false;

    const u8* pix_start = payload + 40;

    auto* pix = static_cast<u32*>(mm::Heap::allocate(static_cast<usize>(uw) * uh * sizeof(u32)));
    if (!pix)
        return false;

    for (u32 y = 0; y < uh; ++y)
    {
        // ICO DIBs are bottom-up.
        const u32 src_y = uh - 1u - y;
        const u8* row = pix_start + src_y * row_stride;
        for (u32 x = 0; x < uw; ++x)
        {
            const u8* p = row + x * (bpp / 8u);
            const u32 bb = p[0];
            const u32 gg = p[1];
            const u32 rr = p[2];
            pix[static_cast<usize>(y) * uw + x] = (rr << 16) | (gg << 8) | bb;
        }
    }

    out.width = uw;
    out.height = uh;
    out.pixels = pix;
    out.owned = true;
    return true;
}

} // namespace notyvos::img
