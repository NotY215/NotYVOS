#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{

namespace
{

inline u16 rd_le16(const u8* p) noexcept
{
    return static_cast<u16>(p[0] | (p[1] << 8));
}

// LZW decoder for GIF. Writes `expected` palette indices into `out`.
bool lzw_decode(const u8* in, usize in_size, u8 min_code_size, u8* out, usize expected) noexcept
{
    constexpr u32 kMaxCodes = 4096;
    u16 prefix[kMaxCodes] = {};
    u8 suffix[kMaxCodes] = {};
    u8 first[kMaxCodes] = {};

    const u32 clear_code = 1u << min_code_size;
    const u32 end_code = clear_code + 1u;
    u32 code_size = min_code_size + 1u;
    u32 next_code = end_code + 1u;

    for (u32 i = 0; i < clear_code; ++i)
    {
        suffix[i] = static_cast<u8>(i);
        first[i] = static_cast<u8>(i);
    }

    usize written = 0;
    usize bit_pos = 0;
    u32 prev = 0;
    bool have_prev = false;
    u8 stack[4096];
    u32 stack_size = 0;

    auto read_code = [&](void) -> i32
    {
        const u32 bits = code_size;
        if (bit_pos + bits > in_size * 8u)
            return -1;
        u32 code = 0;
        for (u32 i = 0; i < bits; ++i)
        {
            const usize bp = bit_pos + i;
            const u8 byte = in[bp >> 3];
            const u32 bit = (byte >> (bp & 7u)) & 1u;
            code |= bit << i;
        }
        bit_pos += bits;
        return static_cast<i32>(code);
    };

    for (;;)
    {
        const i32 cc = read_code();
        if (cc < 0)
            return false;
        const u32 code = static_cast<u32>(cc);

        if (code == clear_code)
        {
            code_size = min_code_size + 1u;
            next_code = end_code + 1u;
            have_prev = false;
            continue;
        }
        if (code == end_code)
            break;

        u32 cur = code;
        stack_size = 0;
        if (cur >= next_code)
        {
            if (!have_prev)
                return false;
            stack[stack_size++] = first[prev];
            cur = prev;
        }
        while (cur >= clear_code && cur < kMaxCodes && cur != end_code)
        {
            stack[stack_size++] = suffix[cur];
            cur = prefix[cur];
            if (stack_size >= sizeof(stack))
                return false;
        }
        if (cur >= kMaxCodes)
            return false;
        stack[stack_size++] = static_cast<u8>(cur);
        const u8 first_char = static_cast<u8>(cur);

        // Emit reversed.
        while (stack_size > 0 && written < expected)
        {
            out[written++] = stack[--stack_size];
        }

        if (have_prev && next_code < kMaxCodes)
        {
            prefix[next_code] = static_cast<u16>(prev);
            suffix[next_code] = first_char;
            first[next_code] = first[prev];
            ++next_code;
            if (next_code == (1u << code_size) && code_size < 12u)
                ++code_size;
        }
        prev = code;
        have_prev = true;
    }
    return written == expected;
}

// Skip a GIF sub-block chain, returning the total byte length of the
// chain's payload (excluding terminators).
u32 sub_block_span(const u8* p, usize remaining) noexcept
{
    u32 total = 0;
    usize pos = 0;
    while (pos < remaining)
    {
        const u8 sz = p[pos];
        ++pos;
        if (sz == 0)
            break;
        if (pos + sz > remaining)
            return 0;
        pos += sz;
        total += sz;
    }
    return total;
}

} // namespace

bool decode_gif_impl(const void* data, usize size, Image& out) noexcept
{
    if (size < 13)
        return false;
    const auto* b = static_cast<const u8*>(data);
    if (libk::memcmp(b, "GIF8", 4) != 0)
        return false;

    const u16 screen_w = rd_le16(b + 6);
    const u16 screen_h = rd_le16(b + 8);
    const u8 flags = b[10];
    const u32 gct_size = (flags & 0x80u) ? (2u << (flags & 0x07u)) : 0u;

    if (screen_w == 0 || screen_h == 0)
        return false;
    if (screen_w > 4096 || screen_h > 4096)
        return false;

    usize pos = 13;
    u8 gct[256 * 3] = {};
    if (gct_size > 0)
    {
        if (pos + gct_size * 3u > size)
            return false;
        for (u32 i = 0; i < gct_size * 3u; ++i)
            gct[i] = b[pos + i];
        pos += gct_size * 3u;
    }

    // We only decode the first image block. Animation is out of scope.
    for (;;)
    {
        if (pos >= size)
            return false;
        const u8 sep = b[pos++];

        if (sep == 0x3Bu)
            return false; // trailer without image

        if (sep == 0x21u) // extension
        {
            if (pos >= size)
                return false;
            ++pos; // extension label
            const u32 span = sub_block_span(b + pos, size - pos);
            // Find the actual position past the terminating 0 byte.
            usize walk = pos;
            for (;;)
            {
                if (walk >= size)
                    return false;
                const u8 sz = b[walk];
                ++walk;
                if (sz == 0)
                    break;
                walk += sz;
            }
            pos = walk;
            (void)span;
            continue;
        }

        if (sep != 0x2Cu)
            return false; // unknown block

        // Image descriptor.
        if (pos + 9 > size)
            return false;
        const u16 img_x = rd_le16(b + pos + 0);
        const u16 img_y = rd_le16(b + pos + 2);
        const u16 img_w = rd_le16(b + pos + 4);
        const u16 img_h = rd_le16(b + pos + 6);
        const u8 iflags = b[pos + 8];
        pos += 9;

        if (img_w == 0 || img_h == 0)
            return false;
        if (img_x + img_w > screen_w || img_y + img_h > screen_h)
            return false;

        u8 lct[256 * 3] = {};
        u32 lct_size = 0;
        if (iflags & 0x80u)
        {
            lct_size = 2u << (iflags & 0x07u);
            if (pos + lct_size * 3u > size)
                return false;
            for (u32 i = 0; i < lct_size * 3u; ++i)
                lct[i] = b[pos + i];
            pos += lct_size * 3u;
        }
        const bool interlaced = (iflags & 0x40u) != 0;

        const u8* active_ct = lct_size ? lct : gct;
        const u32 active_n = lct_size ? lct_size : gct_size;
        if (active_n == 0)
            return false;

        // LZW minimum code size.
        if (pos >= size)
            return false;
        const u8 min_code_size = b[pos++];

        // Collect sub-blocks into a contiguous LZW stream.
        u32 lzw_len = 0;
        {
            usize walk = pos;
            for (;;)
            {
                if (walk >= size)
                    return false;
                const u8 sz = b[walk];
                ++walk;
                if (sz == 0)
                    break;
                if (walk + sz > size)
                    return false;
                lzw_len += sz;
                walk += sz;
            }
        }
        auto* lzw = static_cast<u8*>(mm::Heap::allocate(lzw_len ? lzw_len : 1u));
        if (!lzw)
            return false;
        {
            usize walk = pos;
            u32 wp = 0;
            for (;;)
            {
                const u8 sz = b[walk];
                ++walk;
                if (sz == 0)
                    break;
                libk::memcpy(lzw + wp, b + walk, sz);
                wp += sz;
                walk += sz;
            }
            pos = walk;
        }

        // LZW decode into index buffer.
        const usize n_pixels = static_cast<usize>(img_w) * img_h;
        auto* idx = static_cast<u8*>(mm::Heap::allocate(n_pixels ? n_pixels : 1u));
        if (!idx)
        {
            mm::Heap::deallocate(lzw);
            return false;
        }

        if (!lzw_decode(lzw, lzw_len, min_code_size, idx, n_pixels))
        {
            mm::Heap::deallocate(lzw);
            mm::Heap::deallocate(idx);
            return false;
        }
        mm::Heap::deallocate(lzw);

        // Composite onto a screen-sized surface (opaque).
        auto* pix = static_cast<u32*>(
            mm::Heap::allocate(static_cast<usize>(screen_w) * screen_h * sizeof(u32)));
        if (!pix)
        {
            mm::Heap::deallocate(idx);
            return false;
        }
        for (usize i = 0; i < static_cast<usize>(screen_w) * screen_h; ++i)
            pix[i] = 0;

        // De-interlace if needed. Standard GIF interlace order.
        const u32 passes[4][2] = {{0, 8}, {4, 8}, {2, 4}, {1, 2}};
        u32 src_y = 0;
        for (u32 pass = 0; pass < (interlaced ? 4u : 1u); ++pass)
        {
            const u32 start = interlaced ? passes[pass][0] : 0u;
            const u32 step = interlaced ? passes[pass][1] : 1u;
            for (u32 y = start; y < img_h; y += step)
            {
                for (u32 x = 0; x < img_w; ++x)
                {
                    const u8 ci = idx[static_cast<usize>(src_y) * img_w + x];
                    if (ci >= active_n)
                        continue;
                    const u32 r = active_ct[ci * 3u + 0u];
                    const u32 g = active_ct[ci * 3u + 1u];
                    const u32 bch = active_ct[ci * 3u + 2u];
                    pix[static_cast<usize>(y + img_y) * screen_w + (x + img_x)] =
                        (r << 16) | (g << 8) | bch;
                }
                ++src_y;
            }
        }
        mm::Heap::deallocate(idx);

        out.width = screen_w;
        out.height = screen_h;
        out.pixels = pix;
        out.owned = true;
        return true;
    }
}

} // namespace notyvos::img
