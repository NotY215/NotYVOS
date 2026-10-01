#include <kernel/img/inflate.hpp>
#include <kernel/libk/mem.hpp>

namespace notyvos::img::inflate
{

namespace
{

struct BitReader
{
    const u8* src;
    usize size;
    usize byte_pos;
    u32 bit_buf;
    u32 bit_cnt;
};

inline u32 br_bit(BitReader& br) noexcept
{
    if (br.bit_cnt == 0)
    {
        if (br.byte_pos >= br.size)
            return 0xFFFFFFFFu;
        br.bit_buf = br.src[br.byte_pos++];
        br.bit_cnt = 8;
    }
    const u32 v = br.bit_buf & 1u;
    br.bit_buf >>= 1;
    --br.bit_cnt;
    return v;
}

inline u32 br_bits(BitReader& br, u32 n) noexcept
{
    u32 v = 0;
    for (u32 i = 0; i < n; ++i)
    {
        const u32 b = br_bit(br);
        if (b == 0xFFFFFFFFu)
            return 0xFFFFFFFFu;
        v |= b << i;
    }
    return v;
}

inline void br_align(BitReader& br) noexcept
{
    br.bit_cnt = 0;
    br.bit_buf = 0;
}

// Canonical Huffman table. `limit[l]` / `offset[l]` / `symbols[]` follow
// the classic zlib puff.c layout.
constexpr u32 kMaxBits = 15;

struct Huff
{
    u16 count[kMaxBits + 1];
    u16 symbol[288];
};

bool huff_build(Huff& h, const u8* lengths, u32 n) noexcept
{
    for (u32 i = 0; i <= kMaxBits; ++i)
        h.count[i] = 0;
    for (u32 i = 0; i < n; ++i)
        h.count[lengths[i]]++;
    if (h.count[0] == n)
        return false; // no codes
    u16 left = 1;
    for (u32 len = 1; len <= kMaxBits; ++len)
    {
        left = static_cast<u16>(left << 1);
        if (h.count[len] > left)
            return false;
        left = static_cast<u16>(left - h.count[len]);
    }
    u16 offs[kMaxBits + 1];
    offs[1] = 0;
    for (u32 len = 1; len < kMaxBits; ++len)
        offs[len + 1] = static_cast<u16>(offs[len] + h.count[len]);
    for (u32 i = 0; i < n; ++i)
        if (lengths[i])
            h.symbol[offs[lengths[i]]++] = static_cast<u16>(i);
    return true;
}

// Decode one symbol from the bitstream. Returns -1 on error.
i32 huff_decode(BitReader& br, const Huff& h) noexcept
{
    i32 code = 0, first = 0, index = 0;
    for (u32 len = 1; len <= kMaxBits; ++len)
    {
        const u32 b = br_bit(br);
        if (b == 0xFFFFFFFFu)
            return -1;
        code |= static_cast<i32>(b);
        const i32 count = h.count[len];
        if (code - count < first)
            return h.symbol[static_cast<u32>(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr u32 kMaxCodes = 288;
constexpr u32 kMaxDistCodes = 30;

void build_fixed(Huff& lit, Huff& dist) noexcept
{
    u8 l_lit[kMaxCodes];
    for (u32 i = 0; i < 144; ++i)
        l_lit[i] = 8;
    for (u32 i = 144; i < 256; ++i)
        l_lit[i] = 9;
    for (u32 i = 256; i < 280; ++i)
        l_lit[i] = 7;
    for (u32 i = 280; i < 288; ++i)
        l_lit[i] = 8;
    (void)huff_build(lit, l_lit, 288);
    u8 l_dist[kMaxDistCodes];
    for (u32 i = 0; i < kMaxDistCodes; ++i)
        l_dist[i] = 5;
    (void)huff_build(dist, l_dist, kMaxDistCodes);
}

// Length code lookup (RFC 1951 section 3.2.5).
const u16 kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                          31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const u8 kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                          2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const u16 kDistBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                           33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const u8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Shortest-first ordering for the code-length alphabet.
const u8 kCodeLenOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

} // namespace

usize decompress_raw(const u8* in, usize in_size, u8* out, usize out_cap) noexcept
{
    BitReader br{in, in_size, 0, 0, 0};
    usize out_pos = 0;

    for (;;)
    {
        const u32 bfinal = br_bits(br, 1);
        if (bfinal == 0xFFFFFFFFu)
            return 0;
        const u32 btype = br_bits(br, 2);
        if (btype == 0xFFFFFFFFu)
            return 0;

        if (btype == 0u)
        {
            br_align(br);
            if (br.byte_pos + 4 > br.size)
                return 0;
            const u32 len = static_cast<u32>(br.src[br.byte_pos]) |
                            (static_cast<u32>(br.src[br.byte_pos + 1]) << 8);
            br.byte_pos += 4;
            if (br.byte_pos + len > br.size)
                return 0;
            if (out_pos + len > out_cap)
                return 0;
            for (u32 i = 0; i < len; ++i)
                out[out_pos++] = br.src[br.byte_pos + i];
            br.byte_pos += len;
        }
        else if (btype == 1u || btype == 2u)
        {
            Huff lit{}, dist{};
            if (btype == 1u)
            {
                build_fixed(lit, dist);
            }
            else
            {
                const u32 hlit = br_bits(br, 5);
                const u32 hdist = br_bits(br, 5);
                const u32 hclen = br_bits(br, 4);
                if (hlit == 0xFFFFFFFFu || hdist == 0xFFFFFFFFu || hclen == 0xFFFFFFFFu)
                    return 0;
                const u32 nlit = hlit + 257u;
                const u32 ndist = hdist + 1u;
                const u32 nclen = hclen + 4u;

                u8 clen[19] = {0};
                for (u32 i = 0; i < nclen; ++i)
                {
                    const u32 v = br_bits(br, 3);
                    if (v == 0xFFFFFFFFu)
                        return 0;
                    clen[kCodeLenOrder[i]] = static_cast<u8>(v);
                }
                Huff code_len_huff{};
                if (!huff_build(code_len_huff, clen, 19))
                    return 0;

                u8 lens[320] = {0};
                u32 n = 0;
                while (n < nlit + ndist)
                {
                    const i32 sym = huff_decode(br, code_len_huff);
                    if (sym < 0)
                        return 0;
                    if (sym < 16)
                    {
                        lens[n++] = static_cast<u8>(sym);
                    }
                    else if (sym == 16)
                    {
                        if (n == 0)
                            return 0;
                        const u32 rep = br_bits(br, 2);
                        if (rep == 0xFFFFFFFFu)
                            return 0;
                        const u8 prev = lens[n - 1];
                        for (u32 i = 0; i < rep + 3u && n < nlit + ndist; ++i)
                            lens[n++] = prev;
                    }
                    else if (sym == 17)
                    {
                        const u32 rep = br_bits(br, 3);
                        if (rep == 0xFFFFFFFFu)
                            return 0;
                        for (u32 i = 0; i < rep + 3u && n < nlit + ndist; ++i)
                            lens[n++] = 0;
                    }
                    else if (sym == 18)
                    {
                        const u32 rep = br_bits(br, 7);
                        if (rep == 0xFFFFFFFFu)
                            return 0;
                        for (u32 i = 0; i < rep + 11u && n < nlit + ndist; ++i)
                            lens[n++] = 0;
                    }
                    else
                        return 0;
                }
                if (!huff_build(lit, lens, nlit))
                    return 0;
                if (!huff_build(dist, lens + nlit, ndist))
                    return 0;
            }

            // Decode literals and length/distance pairs.
            for (;;)
            {
                const i32 sym = huff_decode(br, lit);
                if (sym < 0)
                    return 0;
                if (sym < 256)
                {
                    if (out_pos >= out_cap)
                        return 0;
                    out[out_pos++] = static_cast<u8>(sym);
                }
                else if (sym == 256)
                {
                    break;
                }
                else
                {
                    const u32 lidx = static_cast<u32>(sym - 257);
                    if (lidx >= 29u)
                        return 0;
                    u32 len = kLenBase[lidx];
                    if (kLenExtra[lidx])
                    {
                        const u32 extra = br_bits(br, kLenExtra[lidx]);
                        if (extra == 0xFFFFFFFFu)
                            return 0;
                        len += extra;
                    }
                    const i32 dsym = huff_decode(br, dist);
                    if (dsym < 0 || dsym >= 30)
                        return 0;
                    u32 d = kDistBase[static_cast<u32>(dsym)];
                    if (kDistExtra[static_cast<u32>(dsym)])
                    {
                        const u32 extra = br_bits(br, kDistExtra[static_cast<u32>(dsym)]);
                        if (extra == 0xFFFFFFFFu)
                            return 0;
                        d += extra;
                    }
                    if (d == 0 || d > out_pos)
                        return 0;
                    if (out_pos + len > out_cap)
                        return 0;
                    for (u32 i = 0; i < len; ++i)
                    {
                        out[out_pos] = out[out_pos - d];
                        ++out_pos;
                    }
                }
            }
        }
        else
            return 0;

        if (bfinal == 1u)
            break;
    }
    return out_pos;
}

usize decompress_zlib(const u8* in, usize in_size, u8* out, usize out_cap) noexcept
{
    if (in_size < 6)
        return 0;
    // CMF/FLG. The low nibble of CMF must be 8 (DEFLATE); we ignore the
    // preset dictionary bit since PNG never uses it.
    if ((in[0] & 0x0Fu) != 8u)
        return 0;
    return decompress_raw(in + 2, in_size - 2, out, out_cap);
}

} // namespace notyvos::img::inflate
