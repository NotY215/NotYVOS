#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{
// Forward declaration - decode_png_impl lives in png.cpp.
bool decode_png_impl(const void* data, usize size, Image& out) noexcept;
} // namespace notyvos::img

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

// Decode one directory entry into `out`. Returns true on success.
bool decode_entry(const u8* b, usize size, u32 entry_offset, u32 entry_size, Image& out) noexcept
{
    if (entry_offset + entry_size > size || entry_size < 8)
        return false;

    const u8* payload = b + entry_offset;

    // Embedded PNG?
    static const u8 png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (libk::memcmp(payload, png_sig, 8) == 0)
        return decode_png_impl(payload, entry_size, out);

    // DIB - read the header size to know which variant.
    if (entry_size < 12)
        return false;
    const u32 hdr_size = rd_le32(payload);

    i32 w = 0, raw_h = 0;
    u16 bpp = 0;
    u32 palette_start = 0;
    u32 palette_entries = 0;
    u32 pix_start = 0;

    if (hdr_size == 12)
    {
        // BITMAPCOREHEADER.
        w = static_cast<i16>(rd_le16(payload + 4));
        raw_h = static_cast<i16>(rd_le16(payload + 6));
        bpp = rd_le16(payload + 10);
        palette_start = 12;
        palette_entries = (bpp <= 8) ? (1u << bpp) : 0u;
        pix_start = palette_start + palette_entries * 3u;
    }
    else if (hdr_size >= 40)
    {
        // BITMAPINFOHEADER or larger.
        w = static_cast<i32>(rd_le32(payload + 4));
        raw_h = static_cast<i32>(rd_le32(payload + 8));
        bpp = rd_le16(payload + 14);
        palette_start = hdr_size;
        palette_entries = rd_le32(payload + 32);
        if (palette_entries == 0 && bpp <= 8)
            palette_entries = 1u << bpp;
        // BITMAPCOREHEADER uses 3-byte palette entries; everything else uses 4.
        pix_start = palette_start + palette_entries * 4u;
    }
    else
    {
        return false;
    }

    if (w <= 0 || raw_h <= 0)
        return false;
    const u32 uw = static_cast<u32>(w);
    const u32 uh = static_cast<u32>(raw_h / 2); // DIB height is doubled
    if (uw == 0 || uh == 0 || uw > 512 || uh > 512)
        return false;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return false;

    const u32 row_stride = ((uw * bpp + 31u) / 32u) * 4u;
    const u64 needed = pix_start + static_cast<u64>(row_stride) * uh;
    if (needed > entry_size)
        return false;

    // Read palette.
    u8 pal_r[256] = {0}, pal_g[256] = {0}, pal_b[256] = {0};
    if (bpp <= 8 && palette_entries > 0 && palette_entries <= 256)
    {
        const u32 stride = (hdr_size == 12) ? 3u : 4u;
        for (u32 pi = 0; pi < palette_entries; ++pi)
        {
            const u8* pe = payload + palette_start + pi * stride;
            if (payload + palette_start + pi * stride + stride > payload + entry_size)
                break;
            pal_b[pi] = pe[0];
            pal_g[pi] = pe[1];
            pal_r[pi] = pe[2];
        }
    }

    auto* pix = static_cast<u32*>(mm::Heap::allocate(static_cast<usize>(uw) * uh * sizeof(u32)));
    if (!pix)
        return false;

    for (u32 y = 0; y < uh; ++y)
    {
        const u32 src_y = uh - 1u - y; // bottom-up
        const u8* row = payload + pix_start + src_y * row_stride;
        for (u32 x = 0; x < uw; ++x)
        {
            u32 r = 0, g = 0, bl = 0;
            switch (bpp)
            {
            case 32:
            {
                const u8* p = row + x * 4;
                bl = p[0];
                g = p[1];
                r = p[2];
                break;
            }
            case 24:
            {
                const u8* p = row + x * 3;
                bl = p[0];
                g = p[1];
                r = p[2];
                break;
            }
            case 16:
            {
                const u16 v = static_cast<u16>(row[x * 2] | (row[x * 2 + 1] << 8));
                r = static_cast<u32>(((v >> 11) & 0x1Fu) * 255u / 31u);
                g = static_cast<u32>(((v >> 5) & 0x3Fu) * 255u / 63u);
                bl = static_cast<u32>((v & 0x1Fu) * 255u / 31u);
                break;
            }
            case 8:
            {
                const u8 idx = row[x];
                r = pal_r[idx];
                g = pal_g[idx];
                bl = pal_b[idx];
                break;
            }
            case 4:
            {
                const u8 byte = row[x / 2];
                const u8 idx = (x & 1) ? (byte & 0x0Fu) : ((byte >> 4) & 0x0Fu);
                r = pal_r[idx];
                g = pal_g[idx];
                bl = pal_b[idx];
                break;
            }
            case 1:
            {
                const u8 byte = row[x / 8];
                const u8 idx = static_cast<u8>((byte >> (7 - (x & 7))) & 1u);
                r = pal_r[idx];
                g = pal_g[idx];
                bl = pal_b[idx];
                break;
            }
            default:
                break;
            }
            pix[static_cast<usize>(y) * uw + x] = (r << 16) | (g << 8) | bl;
        }
    }

    out.width = uw;
    out.height = uh;
    out.pixels = pix;
    out.owned = true;
    return true;
}

} // namespace

// ICO is a container. Walk the directory in reverse so we usually try the
// largest entry first; fall back to any that decodes.
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
    if (6u + static_cast<u32>(count) * 16u > size)
        return false;

    for (i32 i = static_cast<i32>(count) - 1; i >= 0; --i)
    {
        const u8* de = b + 6 + static_cast<u32>(i) * 16u;
        const u32 entry_size = rd_le32(de + 8);
        const u32 entry_offset = rd_le32(de + 12);
        if (decode_entry(b, size, entry_offset, entry_size, out))
            return true;
    }
    return false;
}

} // namespace notyvos::img
