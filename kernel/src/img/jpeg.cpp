#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{

namespace
{

// ---------------------------------------------------------------------------
// Marker constants (baseline only — SOF0).
// ---------------------------------------------------------------------------
constexpr u8 kSOI = 0xD8;
constexpr u8 kEOI = 0xD9;
constexpr u8 kSOS = 0xDA;
constexpr u8 kDQT = 0xDB;
constexpr u8 kDHT = 0xC4;
constexpr u8 kSOF0 = 0xC0;

// Zigzag scan: index k -> natural coefficient position.
constexpr u8 kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                            12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                            35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                            58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

constexpr u32 kMaxComponents = 4;
constexpr u32 kMaxHuffTables = 4;
constexpr u32 kMaxQuantTables = 4;

// ---------------------------------------------------------------------------
// Byte reader.
// ---------------------------------------------------------------------------
struct Bytes
{
    const u8* p;
    const u8* end;
    bool eof() const noexcept
    {
        return p >= end;
    }
    u8 u8() noexcept
    {
        return p < end ? *p++ : 0u;
    }
    u16 be16() noexcept
    {
        const u16 hi = u8();
        const u16 lo = u8();
        return static_cast<u16>((hi << 8) | lo);
    }
    void skip(u32 n) noexcept
    {
        if (static_cast<usize>(end - p) < n)
            p = end;
        else
            p += n;
    }
};

// ---------------------------------------------------------------------------
// Canonical Huffman table.
// ---------------------------------------------------------------------------
struct Huff
{
    u16 count[17];
    u16 symbol[256];
};

// ---------------------------------------------------------------------------
// Bit reader over entropy-coded data. Handles JPEG byte stuffing (FF 00)
// and skips restart markers (FF D0..D7).
// ---------------------------------------------------------------------------
struct BitReader
{
    const u8* p;
    const u8* end;
    u32 buf; // holds up to 24 valid bits in the low positions
    u32 cnt;

    void refill() noexcept
    {
        while (cnt <= 16 && p < end)
        {
            const u8 b = *p;
            if (b == 0xFF)
            {
                if (p + 1 >= end)
                    break;
                const u8 nx = p[1];
                if (nx == 0x00)
                {
                    p += 2;
                }
                else if (nx >= 0xD0 && nx <= 0xD7)
                {
                    p += 2;
                    continue;
                }
                else
                    break; // real marker begins; stop feeding bits
            }
            else
            {
                ++p;
            }
            buf = ((buf << 8) | b) & 0x00FFFFFFu;
            cnt += 8;
        }
    }

    i32 get(u32 n) noexcept
    {
        if (n == 0)
            return 0;
        if (cnt < n)
            refill();
        if (cnt < n)
            return -1;
        cnt -= n;
        return static_cast<i32>((buf >> cnt) & ((1u << n) - 1u));
    }
};

// Decode one Huffman symbol.
i32 huff_decode(BitReader& br, const Huff& h) noexcept
{
    i32 code = 0;
    i32 first = 0;
    i32 index = 0;
    for (u32 len = 1; len <= 16; ++len)
    {
        const i32 bit = br.get(1);
        if (bit < 0)
            return -1;
        code = (code << 1) | bit;
        const i32 cnt = h.count[len];
        if (code - cnt < first)
            return h.symbol[index + (code - first)];
        index += cnt;
        first = (first + cnt) << 1;
    }
    return -1;
}

// JPEG "extend": sign-extend an `t`-bit magnitude value.
inline i32 extend(i32 v, u32 t) noexcept
{
    if (t == 0)
        return 0;
    if (v < (1 << (t - 1)))
        return v + 1 - (1 << t);
    return v;
}

// ---------------------------------------------------------------------------
// IDCT (integer, from the public-domain stb_image / jidctint lineage).
// ---------------------------------------------------------------------------
constexpr i32 F2F(float x) noexcept
{
    return static_cast<i32>(x * 4096.0f + 0.5f);
}

#define NOTYVOS_IDCT_1D(s0, s1, s2, s3, s4, s5, s6, s7)                                            \
    i32 t0, t1, t2, t3, p1, p2, p3, p4, p5, x0, x1, x2, x3;                                        \
    p2 = s2;                                                                                       \
    p3 = s6;                                                                                       \
    p1 = (p2 + p3) * F2F(0.5411961f);                                                              \
    t2 = p1 + p3 * F2F(-1.847759065f);                                                             \
    t3 = p1 + p2 * F2F(0.765366865f);                                                              \
    p2 = s0;                                                                                       \
    p3 = s4;                                                                                       \
    t0 = (p2 + p3) << 12;                                                                          \
    t1 = (p2 - p3) << 12;                                                                          \
    x0 = t0 + t3;                                                                                  \
    x3 = t0 - t3;                                                                                  \
    x1 = t1 + t2;                                                                                  \
    x2 = t1 - t2;                                                                                  \
    t0 = s7;                                                                                       \
    t1 = s5;                                                                                       \
    t2 = s3;                                                                                       \
    t3 = s1;                                                                                       \
    p3 = t0 + t2;                                                                                  \
    p4 = t1 + t3;                                                                                  \
    p1 = t0 + t3;                                                                                  \
    p2 = t1 + t2;                                                                                  \
    p5 = (p3 + p4) * F2F(1.175875602f);                                                            \
    t0 = t0 * F2F(0.298631336f);                                                                   \
    t1 = t1 * F2F(2.053119869f);                                                                   \
    t2 = t2 * F2F(3.072711026f);                                                                   \
    t3 = t3 * F2F(1.501321110f);                                                                   \
    p1 = p5 + p1 * F2F(-0.899976223f);                                                             \
    p2 = p5 + p2 * F2F(-2.562915447f);                                                             \
    p3 = p3 * F2F(-1.961570560f);                                                                  \
    p4 = p4 * F2F(-0.390180644f);                                                                  \
    t3 += p1 + p4;                                                                                 \
    t2 += p2 + p3;                                                                                 \
    t1 += p2 + p4;                                                                                 \
    t0 += p1 + p3;

inline u8 clamp_u8(i32 v) noexcept
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return static_cast<u8>(v);
}

// In-place 8x8 IDCT of an int16 block, writing u8 samples to `out`
// with row pitch `stride`.
void idct_8x8(const i16* blk, u8* out, u32 stride) noexcept
{
    i32 tmp[64];

    // Column pass.
    for (u32 i = 0; i < 8; ++i)
    {
        const i16* d = blk + i;
        i32* v = tmp + i;
        if (d[8] == 0 && d[16] == 0 && d[24] == 0 && d[32] == 0 && d[40] == 0 && d[48] == 0 &&
            d[56] == 0)
        {
            const i32 t = d[0] * 4;
            v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = t;
        }
        else
        {
            NOTYVOS_IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512;
            x1 += 512;
            x2 += 512;
            x3 += 512;
            v[0] = (x0 + t3) >> 10;
            v[56] = (x0 - t3) >> 10;
            v[8] = (x1 + t2) >> 10;
            v[48] = (x1 - t2) >> 10;
            v[16] = (x2 + t1) >> 10;
            v[40] = (x2 - t1) >> 10;
            v[24] = (x3 + t0) >> 10;
            v[32] = (x3 - t0) >> 10;
        }
    }

    // Row pass.
    for (u32 i = 0; i < 8; ++i)
    {
        const i32* v = tmp + i * 8;
        u8* o = out + i * stride;
        NOTYVOS_IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128 << 17);
        x1 += 65536 + (128 << 17);
        x2 += 65536 + (128 << 17);
        x3 += 65536 + (128 << 17);
        o[0] = clamp_u8((x0 + t3) >> 17);
        o[7] = clamp_u8((x0 - t3) >> 17);
        o[1] = clamp_u8((x1 + t2) >> 17);
        o[6] = clamp_u8((x1 - t2) >> 17);
        o[2] = clamp_u8((x2 + t1) >> 17);
        o[5] = clamp_u8((x2 - t1) >> 17);
        o[3] = clamp_u8((x3 + t0) >> 17);
        o[4] = clamp_u8((x3 - t0) >> 17);
    }
}

#undef NOTYVOS_IDCT_1D

// ---------------------------------------------------------------------------
// Component.
// ---------------------------------------------------------------------------
struct Component
{
    u8 id;
    u8 h_samp, v_samp;
    u8 tq;
    u8 td, ta;
    u8* data;     // block-aligned scan buffer
    u32 padded_w; // = mcu_cols * h_samp * 8
    u32 padded_h; // = mcu_rows * v_samp * 8
    i32 dc_pred;
};

} // namespace

bool decode_jpeg_impl(const void* data, usize size, Image& out) noexcept
{
    if (!data || size < 4)
        return false;
    const auto* base = static_cast<const u8*>(data);

    if (base[0] != 0xFF || base[1] != kSOI)
        return false;

    Bytes br{base + 2, base + size};

    // Quant tables (already de-zigzagged).
    u16 quant[kMaxQuantTables][64];
    bool quant_valid[kMaxQuantTables] = {false, false, false, false};

    // Huffman tables.
    Huff huff_dc[kMaxHuffTables] = {};
    Huff huff_ac[kMaxHuffTables] = {};
    bool huff_dc_valid[kMaxHuffTables] = {false, false, false, false};
    bool huff_ac_valid[kMaxHuffTables] = {false, false, false, false};

    // Frame.
    Component comp[kMaxComponents];
    for (u32 i = 0; i < kMaxComponents; ++i)
        comp[i] = {};
    u32 n_components = 0;
    u32 image_w = 0, image_h = 0;
    u32 max_h = 1, max_v = 1;
    bool got_sof = false;

    // Scan buffers.
    u32 mcu_cols = 0, mcu_rows = 0;

    // State machine.
    bool got_sos = false;
    const u8* entropy_start = nullptr;

    while (!br.eof())
    {
        if (br.p[0] != 0xFF)
            return false;
        ++br.p;
        while (!br.eof() && br.p[0] == 0xFF)
            ++br.p;
        if (br.eof())
            break;
        const u8 marker = br.u8();

        if (marker == kEOI)
            break;

        if (marker == kSOI || (marker >= 0xD0 && marker <= 0xD7))
            continue; // stray SOI/RSTn — ignore

        // All other markers have a 2-byte big-endian length.
        if (br.p + 2 > br.end)
            return false;
        const u16 mlen = br.be16();
        if (mlen < 2)
            return false;
        const u8* payload = br.p;
        if (br.p + (mlen - 2) > br.end)
            return false;

        if (marker == kDQT)
        {
            const u8* q = payload;
            const u8* qend = payload + (mlen - 2);
            while (q < qend)
            {
                const u8 pq_tq = *q++;
                const u32 pq = (pq_tq >> 4) & 0x0Fu;
                const u32 tq = pq_tq & 0x0Fu;
                if (tq >= kMaxQuantTables)
                    return false;
                for (u32 i = 0; i < 64; ++i)
                {
                    u16 v = 0;
                    if (pq == 0)
                    {
                        v = *q++;
                    }
                    else if (q + 2 <= qend)
                    {
                        v = static_cast<u16>((q[0] << 8) | q[1]);
                        q += 2;
                    }
                    else
                        return false;
                    quant[tq][kZigzag[i]] = v;
                }
                quant_valid[tq] = true;
            }
        }
        else if (marker == kDHT)
        {
            const u8* q = payload;
            const u8* qend = payload + (mlen - 2);
            while (q < qend)
            {
                const u8 tc_th = *q++;
                const u32 tc = (tc_th >> 4) & 0x0Fu;
                const u32 th = tc_th & 0x0Fu;
                if (th >= kMaxHuffTables)
                    return false;
                Huff* h = (tc == 0) ? &huff_dc[th] : &huff_ac[th];
                for (u32 i = 1; i <= 16; ++i)
                    h->count[i] = 0;
                for (u32 i = 0; i < 256; ++i)
                    h->symbol[i] = 0;

                u32 total = 0;
                for (u32 i = 1; i <= 16; ++i)
                {
                    if (q + 2 > qend)
                        return false;
                    const u16 c = static_cast<u16>((q[0] << 8) | q[1]);
                    q += 2;
                    h->count[i] = c;
                    total += c;
                }
                if (total > 256u)
                    return false;
                if (q + total > qend)
                    return false;
                for (u32 i = 0; i < total; ++i)
                    h->symbol[i] = *q++;

                if (tc == 0)
                    huff_dc_valid[th] = true;
                else
                    huff_ac_valid[th] = true;
            }
        }
        else if (marker == kSOF0)
        {
            if (mlen < 11)
                return false;
            const u8* f = payload;
            const u8 precision = f[0];
            if (precision != 8)
                return false;
            image_h = static_cast<u32>((f[1] << 8) | f[2]);
            image_w = static_cast<u32>((f[3] << 8) | f[4]);
            n_components = f[5];
            if (n_components == 0 || n_components > kMaxComponents)
                return false;
            if (image_w == 0 || image_h == 0)
                return false;
            if (image_w > 4096 || image_h > 4096)
                return false;
            if (mlen < 8 + n_components * 3u)
                return false;

            const u8* c = f + 6;
            max_h = max_v = 1;
            for (u32 i = 0; i < n_components; ++i)
            {
                comp[i].id = c[0];
                const u8 hv = c[1];
                comp[i].h_samp = (hv >> 4) & 0x0Fu;
                comp[i].v_samp = hv & 0x0Fu;
                comp[i].tq = c[2];
                if (comp[i].h_samp == 0 || comp[i].v_samp == 0)
                    return false;
                if (comp[i].h_samp > max_h)
                    max_h = comp[i].h_samp;
                if (comp[i].v_samp > max_v)
                    max_v = comp[i].v_samp;
                c += 3;
            }
            mcu_cols = (image_w + max_h * 8u - 1u) / (max_h * 8u);
            mcu_rows = (image_h + max_v * 8u - 1u) / (max_v * 8u);
            got_sof = true;
        }
        else if (marker == kSOS)
        {
            if (!got_sof)
                return false;
            if (mlen < 6)
                return false;
            const u8* s = payload;
            const u32 ns = s[0];
            if (ns == 0 || ns > n_components)
                return false;
            if (mlen < 1 + ns * 2u + 3u)
                return false;
            const u8* cs = s + 1;
            for (u32 i = 0; i < ns; ++i)
            {
                const u8 id = cs[0];
                const u8 tdta = cs[1];
                const u32 td = (tdta >> 4) & 0x0Fu;
                const u32 ta = tdta & 0x0Fu;
                bool found = false;
                for (u32 k = 0; k < n_components; ++k)
                {
                    if (comp[k].id == id)
                    {
                        comp[k].td = static_cast<u8>(td);
                        comp[k].ta = static_cast<u8>(ta);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
                cs += 2;
            }
            // Ss, Se, Ah/Al — ignored for baseline.
            entropy_start = br.p + (mlen - 2);
            got_sos = true;
            break;
        }
        else
        {
            // Skip unknown APPn / COM / DRI etc.
        }

        br.p = payload + (mlen - 2);
    }

    if (!got_sos || !entropy_start)
        return false;

    // Verify quant and huff tables are all present.
    for (u32 i = 0; i < n_components; ++i)
    {
        if (!quant_valid[comp[i].tq])
            return false;
        if (!huff_dc_valid[comp[i].td])
            return false;
        if (!huff_ac_valid[comp[i].ta])
            return false;
    }

    // Allocate per-component scan buffers.
    for (u32 i = 0; i < n_components; ++i)
    {
        comp[i].padded_w = mcu_cols * comp[i].h_samp * 8u;
        comp[i].padded_h = mcu_rows * comp[i].v_samp * 8u;
        const usize n = static_cast<usize>(comp[i].padded_w) * comp[i].padded_h;
        comp[i].data = static_cast<u8*>(mm::Heap::allocate(n ? n : 1));
        if (!comp[i].data)
            goto cleanup_fail;
        for (usize k = 0; k < n; ++k)
            comp[i].data[k] = 0;
        comp[i].dc_pred = 0;
    }

    {
        // Entropy decode.
        BitReader brt{entropy_start, br.end, 0, 0};
        i16 blk[64];

        for (u32 mcu_y = 0; mcu_y < mcu_rows; ++mcu_y)
        {
            for (u32 mcu_x = 0; mcu_x < mcu_cols; ++mcu_x)
            {
                for (u32 ci = 0; ci < n_components; ++ci)
                {
                    Component& cc = comp[ci];
                    for (u32 vy = 0; vy < cc.v_samp; ++vy)
                    {
                        for (u32 hx = 0; hx < cc.h_samp; ++hx)
                        {
                            for (u32 k = 0; k < 64; ++k)
                                blk[k] = 0;

                            // DC.
                            const i32 dcat = huff_decode(brt, huff_dc[cc.td]);
                            if (dcat < 0)
                                goto cleanup_fail;
                            if (dcat == 0)
                            {
                                cc.dc_pred += 0;
                            }
                            else
                            {
                                const i32 bits = brt.get(static_cast<u32>(dcat));
                                if (bits < 0)
                                    goto cleanup_fail;
                                cc.dc_pred += extend(bits, static_cast<u32>(dcat));
                            }
                            blk[0] = static_cast<i16>(cc.dc_pred);

                            // AC.
                            u32 kk = 1;
                            while (kk < 64)
                            {
                                const i32 rs = huff_decode(brt, huff_ac[cc.ta]);
                                if (rs < 0)
                                    goto cleanup_fail;
                                const u32 rsu = static_cast<u32>(rs);
                                const u32 r = (rsu >> 4) & 0x0Fu;
                                const u32 sz = rsu & 0x0Fu;
                                if (sz == 0)
                                {
                                    if (r == 15)
                                    {
                                        kk += 16;
                                        continue;
                                    }      // ZRL
                                    break; // EOB
                                }
                                kk += r;
                                if (kk >= 64)
                                    break;
                                const i32 bits = brt.get(sz);
                                if (bits < 0)
                                    goto cleanup_fail;
                                const i32 v = extend(bits, sz);
                                blk[kZigzag[kk]] = static_cast<i16>(v);
                                ++kk;
                            }

                            // Dequantize.
                            for (u32 k = 0; k < 64; ++k)
                                blk[k] =
                                    static_cast<i16>(blk[k] * static_cast<i32>(quant[cc.tq][k]));

                            // Write block into scan buffer.
                            const u32 bx = mcu_x * cc.h_samp + hx;
                            const u32 by = mcu_y * cc.v_samp + vy;
                            const u32 px = bx * 8u;
                            const u32 py = by * 8u;
                            u8* dst = cc.data + static_cast<usize>(py) * cc.padded_w + px;
                            idct_8x8(blk, dst, cc.padded_w);
                        }
                    }
                }
            }
        }
    }

    // Compose output image.
    {
        const usize n_pixels = static_cast<usize>(image_w) * image_h;
        auto* pix = static_cast<u32*>(mm::Heap::allocate(n_pixels * sizeof(u32)));
        if (!pix)
            goto cleanup_fail;

        if (n_components == 1)
        {
            const Component& y = comp[0];
            for (u32 yy = 0; yy < image_h; ++yy)
            {
                const u8* row = y.data + static_cast<usize>(yy) * y.padded_w;
                for (u32 xx = 0; xx < image_w; ++xx)
                {
                    const u32 g = row[xx];
                    pix[static_cast<usize>(yy) * image_w + xx] = (g << 16) | (g << 8) | g;
                }
            }
        }
        else
        {
            const Component& y = comp[0];
            const Component& cb = comp[1];
            const Component& cr = comp[2];

            for (u32 yy = 0; yy < image_h; ++yy)
            {
                const u32 cy = yy * cb.v_samp / max_v;
                const u32 cr_y = yy * cr.v_samp / max_v;
                const u8* y_row = y.data + static_cast<usize>(yy) * y.padded_w;
                const u8* cb_row = cb.data + static_cast<usize>(cy) * cb.padded_w;
                const u8* cr_row = cr.data + static_cast<usize>(cr_y) * cr.padded_w;

                for (u32 xx = 0; xx < image_w; ++xx)
                {
                    const u32 cx = xx * cb.h_samp / max_h;
                    const u32 cr_x = xx * cr.h_samp / max_h;
                    const i32 Y = y_row[xx];
                    const i32 Cb = static_cast<i32>(cb_row[cx]) - 128;
                    const i32 Cr = static_cast<i32>(cr_row[cr_x]) - 128;

                    // Integer YCbCr -> RGB (fixed-point 16.16, from stb).
                    const i32 r = Y + ((91881 * Cr) >> 16);
                    const i32 g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                    const i32 b = Y + ((116130 * Cb) >> 16);

                    pix[static_cast<usize>(yy) * image_w + xx] =
                        (static_cast<u32>(clamp_u8(r)) << 16) |
                        (static_cast<u32>(clamp_u8(g)) << 8) | static_cast<u32>(clamp_u8(b));
                }
            }
        }

        out.width = image_w;
        out.height = image_h;
        out.pixels = pix;
        out.owned = true;
        return true;
    }

cleanup_fail:
    for (u32 i = 0; i < n_components; ++i)
        if (comp[i].data)
        {
            mm::Heap::deallocate(comp[i].data);
            comp[i].data = nullptr;
        }
    return false;
}

} // namespace notyvos::img
