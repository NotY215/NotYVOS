#include <kernel/img/decoder.hpp>
#include <kernel/img/inflate.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{

namespace
{

inline u32 rd_be32(const u8* p) noexcept
{
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

inline u8 paeth(i32 a, i32 b, i32 c) noexcept
{
    const i32 p = a + b - c;
    const i32 pa = (p > a) ? (p - a) : (a - p);
    const i32 pb = (p > b) ? (p - b) : (b - p);
    const i32 pc = (p > c) ? (p - c) : (c - p);
    if (pa <= pb && pa <= pc)
        return static_cast<u8>(a);
    if (pb <= pc)
        return static_cast<u8>(b);
    return static_cast<u8>(c);
}

constexpr u32 kMaxDim = 4096;

} // namespace

bool decode_png_impl(const void* data, usize size, Image& out) noexcept
{
    if (size < 8)
        return false;
    const auto* b = static_cast<const u8*>(data);

    // Signature check.
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (libk::memcmp(b, sig, 8) != 0)
        return false;

    // Header fields.
    u32 width = 0, height = 0;
    u8 bit_depth = 0, color_type = 0, interlace = 0;
    bool got_ihdr = false;

    // Accumulate IDAT bytes (concatenated across chunks).
    const u8* idat_ptr[64] = {};
    u32 idat_len[64] = {};
    u32 idat_count = 0;

    // Palette.
    u8 palette[256 * 3] = {};
    u32 palette_count = 0;

    // tRNS simple-alpha for palette images.
    u8 trns[256] = {};
    u32 trns_count = 0;

    usize pos = 8;
    while (pos + 12 <= size)
    {
        const u32 chunk_len = rd_be32(b + pos);
        const char* chunk_id = reinterpret_cast<const char*>(b + pos + 4);
        if (chunk_len > size || pos + 12u + chunk_len > size)
            return false;
        const u8* chunk_data = b + pos + 8;

        if (libk::memcmp(chunk_id, "IHDR", 4) == 0 && chunk_len >= 13u)
        {
            width = rd_be32(chunk_data + 0);
            height = rd_be32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            interlace = chunk_data[12];
            if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim)
                return false;
            if (bit_depth != 8)
                return false; // 16-bit and sub-byte: deferred
            if (interlace != 0)
                return false; // Adam7 deferred
            got_ihdr = true;
        }
        else if (libk::memcmp(chunk_id, "PLTE", 4) == 0)
        {
            const u32 n = (chunk_len / 3u) > 256u ? 256u : (chunk_len / 3u);
            for (u32 i = 0; i < n * 3u; ++i)
                palette[i] = chunk_data[i];
            palette_count = n;
        }
        else if (libk::memcmp(chunk_id, "tRNS", 4) == 0)
        {
            const u32 n = chunk_len > 256u ? 256u : chunk_len;
            for (u32 i = 0; i < n; ++i)
                trns[i] = chunk_data[i];
            trns_count = n;
        }
        else if (libk::memcmp(chunk_id, "IDAT", 4) == 0)
        {
            if (idat_count < 64u)
            {
                idat_ptr[idat_count] = chunk_data;
                idat_len[idat_count] = chunk_len;
                ++idat_count;
            }
        }
        else if (libk::memcmp(chunk_id, "IEND", 4) == 0)
        {
            break;
        }
        pos += 12u + chunk_len;
    }

    if (!got_ihdr || idat_count == 0)
        return false;

    // Channel count per pixel.
    u32 channels = 0;
    switch (color_type)
    {
    case 0:
        channels = 1;
        break; // grayscale
    case 2:
        channels = 3;
        break; // RGB
    case 3:
        channels = 1;
        break; // palette index
    case 4:
        channels = 2;
        break; // gray + alpha
    case 6:
        channels = 4;
        break; // RGBA
    default:
        return false;
    }

    // Concatenate IDAT into a single zlib buffer.
    u64 idat_total = 0;
    for (u32 i = 0; i < idat_count; ++i)
        idat_total += idat_len[i];
    if (idat_total == 0 || idat_total > 64u * 1024u * 1024u)
        return false;

    auto* zdata = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(idat_total)));
    if (!zdata)
        return false;
    {
        usize wp = 0;
        for (u32 i = 0; i < idat_count; ++i)
        {
            libk::memcpy(zdata + wp, idat_ptr[i], idat_len[i]);
            wp += idat_len[i];
        }
    }

    // Raw scanline buffer size: for each row, 1 filter byte + width*channels.
    const u64 row_bytes = 1u + static_cast<u64>(width) * channels;
    const u64 raw_size = row_bytes * height;
    if (raw_size > 128u * 1024u * 1024u)
    {
        mm::Heap::deallocate(zdata);
        return false;
    }

    auto* raw = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(raw_size)));
    if (!raw)
    {
        mm::Heap::deallocate(zdata);
        return false;
    }

    const usize got = inflate::decompress_zlib(zdata, static_cast<usize>(idat_total), raw,
                                               static_cast<usize>(raw_size));
    mm::Heap::deallocate(zdata);
    if (got < raw_size)
    {
        mm::Heap::deallocate(raw);
        return false;
    }

    // Un-filter into a linear pixel buffer.
    const usize n_pixels = static_cast<usize>(width) * height;
    auto* pix = static_cast<u32*>(mm::Heap::allocate(n_pixels * sizeof(u32)));
    if (!pix)
    {
        mm::Heap::deallocate(raw);
        return false;
    }

    const u32 stride = width * channels;
    for (u32 y = 0; y < height; ++y)
    {
        const u8* src = raw + y * row_bytes;
        const u8 filter = src[0];
        const u8* in_row = src + 1;
        const u8* up_row = (y == 0) ? nullptr : raw + (y - 1) * row_bytes + 1;

        for (u32 x = 0; x < stride; ++x)
        {
            const u8 a = (x >= channels) ? in_row[x - channels] : 0;
            const u8 bval = up_row ? up_row[x] : 0;
            const u8 c = (up_row && x >= channels) ? up_row[x - channels] : 0;
            const u8 v = in_row[x];
            u8 out_v = v;
            switch (filter)
            {
            case 0:
                out_v = v;
                break;
            case 1:
                out_v = static_cast<u8>(v + a);
                break;
            case 2:
                out_v = static_cast<u8>(v + bval);
                break;
            case 3:
                out_v = static_cast<u8>(v + ((static_cast<u32>(a) + bval) / 2u));
                break;
            case 4:
                out_v = static_cast<u8>(v + paeth(a, bval, c));
                break;
            default:
                return false;
            }
            // Write the reconstructed byte back into the raw buffer so the
            // next rows' filters can read it via up_row.
            const_cast<u8*>(in_row)[x] = out_v;
        }

        // Expand row into RGBA-ish u32 pixels (0x00RRGGBB).
        for (u32 x = 0; x < width; ++x)
        {
            const u8* p = in_row + x * channels;
            u32 r = 0, g = 0, bch = 0;
            switch (color_type)
            {
            case 0:
                r = g = bch = p[0];
                break;
            case 2:
                r = p[0];
                g = p[1];
                bch = p[2];
                break;
            case 3:
            {
                const u32 idx = p[0];
                if (idx >= palette_count)
                {
                    r = g = bch = 0;
                }
                else
                {
                    r = palette[idx * 3u];
                    g = palette[idx * 3u + 1u];
                    bch = palette[idx * 3u + 2u];
                }
                break;
            }
            case 4:
                r = g = bch = p[0];
                break;
            case 6:
                r = p[0];
                g = p[1];
                bch = p[2];
                break;
            }
            pix[static_cast<usize>(y) * width + x] = (r << 16) | (g << 8) | bch;
        }
    }

    (void)trns;
    (void)trns_count;

    mm::Heap::deallocate(raw);

    out.width = width;
    out.height = height;
    out.pixels = pix;
    out.owned = true;
    return true;
}

} // namespace notyvos::img
