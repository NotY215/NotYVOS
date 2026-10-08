#include <kernel/font/font.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::font
{

namespace
{

// ---------------------------------------------------------------------------
// Big-endian readers.
// ---------------------------------------------------------------------------
inline u16 rd16(const u8* p) noexcept
{ return static_cast<u16>((static_cast<u16>(p[0]) << 8) | p[1]); }
inline i16 rdi16(const u8* p) noexcept
{ return static_cast<i16>(rd16(p)); }
inline u32 rd32(const u8* p) noexcept
{
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16)
         | (static_cast<u32>(p[2]) <<  8) |  static_cast<u32>(p[3]);
}

// ---------------------------------------------------------------------------
// Public struct definitions (declared opaque in the header).
// ---------------------------------------------------------------------------
struct TableRecord
{
    u32 tag;
    u32 offset;
    u32 length;
};

constexpr u32 kMaxTables = 32;
constexpr u32 kMaxGlyphCacheEntries = 512;

struct CacheEntry
{
    u32  codepoint;
    u32  pixel_size;
    u32  age;
    Glyph g;
};

} // namespace

// Face is opaque; Glyph is defined in the header.
struct Face
{
    // Raw file bytes. Owned.
    u8*  data;
    usize size;

    // Table offsets.
    u32 off_head;
    u32 off_hhea;
    u32 off_hmtx;
    u32 off_maxp;
    u32 off_cmap;
    u32 off_loca;
    u32 off_glyf;
    u32 off_os2;      // optional

    // Metrics from head / hhea.
    u32 units_per_em;
    i16 ascent_units;
    i16 descent_units;
    i16 line_gap_units;
    u16 num_h_metrics;
    u16 num_glyphs;
    u16 loca_format;    // 0 = short, 1 = long

    // Cache.
    CacheEntry cache[kMaxGlyphCacheEntries];
    u32        cache_count;
    u32        cache_age;

    // Scratch buffer for coverage bitmaps (reused per call).
    u8*  scratch;
    usize scratch_cap;
};

namespace
{

u64 g_faces_loaded = 0;
u64 g_hits         = 0;
u64 g_misses       = 0;

Face* g_default = nullptr;

// Find a table by tag. Returns 0 if not found.
u32 find_table(const Face* f, u32 tag) noexcept
{
    const u8* p = f->data;
    const u16 n = rd16(p + 4);
    for (u16 i = 0; i < n && i < kMaxTables; ++i)
    {
        const u8* rec = p + 12 + static_cast<u32>(i) * 16;
        if (rd32(rec) == tag) return rd32(rec + 8);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// cmap
// ---------------------------------------------------------------------------
// Look up a glyph index for a Unicode codepoint. Supports format 4 (BMP)
// and format 12 (full Unicode). Returns 0 for .notdef.
u32 cmap_lookup(const Face* f, u32 codepoint) noexcept
{
    if (!f->off_cmap) return 0;
    const u8* p = f->data + f->off_cmap;
    const u16 num_subtables = rd16(p + 2);

    // Prefer a format 12 table; fall back to format 4.
    u32 fmt4_off = 0, fmt12_off = 0;
    for (u16 i = 0; i < num_subtables; ++i)
    {
        const u8* rec = p + 4 + static_cast<u32>(i) * 8;
        const u32 off = f->off_cmap + rd32(rec + 4);
        if (off + 4 > f->size) continue;
        const u16 fmt = rd16(f->data + off);
        if (fmt == 4 && !fmt4_off)  fmt4_off  = off;
        if (fmt == 12 && !fmt12_off) fmt12_off = off;
    }

    if (fmt12_off)
    {
        const u8* t = f->data + fmt12_off;
        const u32 n_groups = rd32(t + 12);
        for (u32 g = 0; g < n_groups; ++g)
        {
            const u8* grp = t + 16 + g * 12;
            const u32 start = rd32(grp + 0);
            const u32 end   = rd32(grp + 4);
            const u32 start_glyph = rd32(grp + 8);
            if (codepoint >= start && codepoint <= end)
                return start_glyph + (codepoint - start);
        }
        return 0;
    }

    if (fmt4_off)
    {
        if (codepoint > 0xFFFFu) return 0;
        const u8* t = f->data + fmt4_off;
        const u16 seg_count = static_cast<u16>(rd16(t + 6) / 2u);
        const u8* end_codes   = t + 14;
        const u8* start_codes = end_codes + seg_count * 2 + 2;
        const u8* id_deltas   = start_codes + seg_count * 2;
        const u8* id_range_off = id_deltas + seg_count * 2;

        for (u16 s = 0; s < seg_count; ++s)
        {
            const u16 end = rd16(end_codes + s * 2);
            if (codepoint > end) continue;
            const u16 start = rd16(start_codes + s * 2);
            if (codepoint < start) return 0;
            const i16 delta = rdi16(id_deltas + s * 2);
            const u16 range_off = rd16(id_range_off + s * 2);
            if (range_off == 0)
                return static_cast<u32>((static_cast<i32>(codepoint) + delta) & 0xFFFF);
            const u8* glyph_off = id_range_off + s * 2 + range_off
                                + (codepoint - start) * 2;
            if (glyph_off + 2 > f->data + f->size) return 0;
            const u16 g = rd16(glyph_off);
            if (g == 0) return 0;
            return static_cast<u32>((static_cast<i32>(g) + delta) & 0xFFFF);
        }
        return 0;
    }
    return 0;
}

// Advance width in font units.
i32 hmtx_advance(const Face* f, u32 glyph_index) noexcept
{
    if (!f->off_hmtx) return 0;
    if (glyph_index >= f->num_glyphs) return 0;
    if (glyph_index < f->num_h_metrics)
        return rdi16(f->data + f->off_hmtx + glyph_index * 4);
    // Beyond num_h_metrics: all share the last advance.
    return rdi16(f->data + f->off_hmtx + (f->num_h_metrics - 1u) * 4);
}

// ---------------------------------------------------------------------------
// Simple glyph outline extraction.
// ---------------------------------------------------------------------------
// A glyph is stored as N contours. Each contour has M points, each point
// is either on-curve (flag bit 0 set) or off-curve (control point for a
// quadratic Bézier).
//
// We expand each contour into a polyline of on-curve points, flattening
// the quadratic Bézier segments into up to 8 line segments each.

constexpr u32 kMaxContours = 32;
constexpr u32 kMaxPoints   = 512;

struct OutlinePoint
{
    i32 x, y;       // font units
    bool on_curve;
};

struct Outline
{
    OutlinePoint pts[kMaxPoints];
    u32  contour_end[kMaxContours];   // index just past the last point of each contour
    u32  point_count;
    u32  contour_count;
    i32  x_min, y_min, x_max, y_max;
};

// Simple glyph parse. Returns false on composite or empty.
bool parse_simple_glyph(const Face* f, u32 glyph_offset, Outline& out) noexcept
{
    if (!f->off_glyf || !f->off_loca) return false;
    const u8* g = f->data + glyph_offset;
    if (glyph_offset + 10 > f->size) return false;

    const i16 n_contours = rdi16(g);
    if (n_contours < 0) return false;   // composite -- handled separately
    if (n_contours == 0) return true;   // empty glyph (space)

    out.x_min = rdi16(g + 2);
    out.y_min = rdi16(g + 4);
    out.x_max = rdi16(g + 6);
    out.y_max = rdi16(g + 8);

    const u8* p = g + 10;
    const u32 nc = static_cast<u32>(n_contours);
    if (nc > kMaxContours) return false;

    u32 total_points = 0;
    for (u32 i = 0; i < nc; ++i)
    {
        const u16 end = rd16(p + i * 2);
        total_points = end + 1u;
    }
    if (total_points > kMaxPoints) return false;

    out.contour_count = nc;
    out.point_count = total_points;

    // End points.
    for (u32 i = 0; i < nc; ++i)
        out.contour_end[i] = rd16(p + i * 2) + 1u;

    p += nc * 2;
    const u16 instruction_len = rd16(p);
    p += 2 + instruction_len;

    // Flags.
    u8 flags[kMaxPoints];
    {
        u32 i = 0;
        while (i < total_points)
        {
            const u8 fl = *p++;
            flags[i++] = fl;
            if (fl & 0x08u)   // REPEAT
            {
                const u8 rep = *p++;
                for (u32 k = 0; k < rep && i < total_points; ++k)
                    flags[i++] = fl;
            }
        }
    }

    // X coordinates.
    i32 xs[kMaxPoints];
    {
        i32 x = 0;
        for (u32 i = 0; i < total_points; ++i)
        {
            if (flags[i] & 0x02u)   // x is 1 byte
            {
                const u8 dx = *p++;
                x += (flags[i] & 0x10u) ? dx : -static_cast<i32>(dx);
            }
            else if (!(flags[i] & 0x10u))   // x is 2-byte delta
            {
                const i16 dx = rdi16(p);
                p += 2;
                x += dx;
            }
            xs[i] = x;
        }
    }

    // Y coordinates.
    i32 ys[kMaxPoints];
    {
        i32 y = 0;
        for (u32 i = 0; i < total_points; ++i)
        {
            if (flags[i] & 0x04u)
            {
                const u8 dy = *p++;
                y += (flags[i] & 0x20u) ? dy : -static_cast<i32>(dy);
            }
            else if (!(flags[i] & 0x20u))
            {
                const i16 dy = rdi16(p);
                p += 2;
                y += dy;
            }
            ys[i] = y;
        }
    }

    for (u32 i = 0; i < total_points; ++i)
    {
        out.pts[i].x = xs[i];
        out.pts[i].y = ys[i];
        out.pts[i].on_curve = (flags[i] & 0x01u) != 0;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Outline → coverage bitmap.
//
// We scale the outline into a target pixel size and rasterize using an
// axis-aligned supersample grid: 4 sub-rows per output row, and exact
// horizontal coverage accumulated from scanline x-intersections.
// ---------------------------------------------------------------------------
struct Edge
{
    i32 x0, y0, x1, y1;   // font-unit points, y0 < y1
};

// Flatten contours into line segments.
void flatten_contours(const Outline& o, i32 scale_num, i32 scale_den,
                      Edge* edges, u32& edge_count, i32& ymin, i32& ymax,
                      u32 max_edges) noexcept
{
    edge_count = 0;
    ymin = 0x7FFFFFFF;
    ymax = -0x7FFFFFFF;

    for (u32 ci = 0; ci < o.contour_count; ++ci)
    {
        const u32 start = (ci == 0) ? 0 : o.contour_end[ci - 1];
        const u32 end   = o.contour_end[ci];
        if (end <= start) continue;

        // Build the on-curve polyline for this contour.
        // We iterate through points, converting consecutive (on, off, on)
        // triples into quadratic Bézier segments.
        struct XY { i32 x, y; };
        XY poly[512];
        u32 n = 0;

        auto to_px = [&](i32 fx, i32 fy) -> XY
        {
            const i64 sx = (static_cast<i64>(fx) * scale_num) / scale_den;
            const i64 sy = (static_cast<i64>(fy) * scale_num) / scale_den;
            return { static_cast<i32>(sx), static_cast<i32>(sy) };
        };

        // Locate the first on-curve point.
        u32 first_on = start;
        for (u32 i = start; i < end; ++i)
            if (o.pts[i].on_curve) { first_on = i; break; }

        // Walk the contour, emitting polyline segments.
        XY cur_px = to_px(o.pts[first_on].x, o.pts[first_on].y);
        poly[n++] = cur_px;

        for (u32 k = 1; k <= end - start; ++k)
        {
            const u32 idx = start + ((first_on - start + k) % (end - start));
            const OutlinePoint& pt = o.pts[idx];
            if (pt.on_curve)
            {
                const XY np = to_px(pt.x, pt.y);
                if (n < 512) poly[n++] = np;
                cur_px = np;
            }
            else
            {
                // Off-curve: quadratic Bézier from cur_px via this point.
                const XY ctrl = to_px(pt.x, pt.y);
                // Find the next point (which is either on-curve or the
                // implicit midpoint for two consecutive off-curve points).
                const u32 nx_idx = start + ((first_on - start + k + 1) % (end - start));
                const OutlinePoint& next = o.pts[nx_idx];
                XY endp;
                if (next.on_curve)
                {
                    endp = to_px(next.x, next.y);
                }
                else
                {
                    // Implicit on-curve at the midpoint.
                    endp = { (ctrl.x + to_px(next.x, next.y).x) / 2,
                             (ctrl.y + to_px(next.x, next.y).y) / 2 };
                }
                // Subdivide into 6 sub-segments (enough for smooth AA).
                for (u32 s = 1; s <= 6; ++s)
                {
                    const i64 t_num = s;
                    const i64 t_den = 6;
                    const i64 t_1 = t_den - t_num;
                    const i64 x = (t_1 * t_1 * cur_px.x + 2 * t_1 * t_num * ctrl.x + t_num * t_num * endp.x) / (t_den * t_den);
                    const i64 y = (t_1 * t_1 * cur_px.y + 2 * t_1 * t_num * ctrl.y + t_num * t_num * endp.y) / (t_den * t_den);
                    if (n < 512) poly[n++] = { static_cast<i32>(x), static_cast<i32>(y) };
                }
                cur_px = endp;
            }
        }

        // Close the contour if the last point doesn't match the first.
        if (n > 1 && (poly[n - 1].x != poly[0].x || poly[n - 1].y != poly[0].y))
        {
            if (n < 512) poly[n++] = poly[0];
        }

        // Emit edges from the polyline.
        for (u32 i = 0; i + 1 < n; ++i)
        {
            XY a = poly[i], b = poly[i + 1];
            if (a.y == b.y) continue;
            if (a.y > b.y) { const XY t = a; a = b; b = t; }
            if (edge_count < max_edges)
            {
                edges[edge_count++] = { a.x, a.y, b.x, b.y };
                if (a.y < ymin) ymin = a.y;
                if (b.y > ymax) ymax = b.y;
            }
        }
    }
}

// Supersampled coverage rasterization. `out` is width x height u8.
void rasterize_edges(const Edge* edges, u32 edge_count,
                     i32 ymin, i32 ymax,
                     i32 origin_x, i32 origin_y,
                     u32 width, u32 height,
                     u8* out) noexcept
{
    for (u32 i = 0; i < width * height; ++i) out[i] = 0;

    constexpr u32 kSubY = 4;
    // For each output row, we supersample vertically with kSubY sub-rows.
    // Each sub-row is scanline-filled with exact horizontal coverage.
    const i32 y_lo = ymin;
    const i32 y_hi = ymax;

    for (i32 row = 0; row < static_cast<i32>(height); ++row)
    {
        // Font-space Y grows up, screen-space Y grows down. Flip the row
        // index so bitmap row 0 corresponds to y_max (top of the glyph).
        const i32 src_row = static_cast<i32>(height) - 1 - row;
        const i32 y0 = (src_row << 8) + origin_y; // top of pixel in 8.8 space
        const i32 y1 = y0 + 256;                  // bottom

        // Vertical overlap with [y_lo, y_hi].
        if (y1 <= y_lo || y0 >= y_hi) continue;

        // Accumulate coverage from kSubY sub-scanlines.
        // For each sub-scanline, we walk all edges, collect x intersections,
        // sort, and add horizontal coverage into row_sums[].
        i32 row_sums[512] = {0};   // caps at 512 pixels wide

        for (u32 sy = 0; sy < kSubY; ++sy)
        {
            const i32 scan = y0 + (static_cast<i32>(sy) * 256) / static_cast<i32>(kSubY) + 32;
            if (scan < y_lo || scan >= y_hi) continue;

            i32 xs[64];
            u32 nx = 0;
            for (u32 e = 0; e < edge_count; ++e)
            {
                const Edge& eg = edges[e];
                if (scan < eg.y0 || scan >= eg.y1) continue;
                const i32 dy = eg.y1 - eg.y0;
                const i32 x = eg.x0 + static_cast<i32>(
                    (static_cast<i64>(eg.x1 - eg.x0) * (scan - eg.y0)) / dy);
                if (nx < 64) xs[nx++] = x;
            }
            for (u32 i = 0; i < nx; ++i)
                for (u32 j = i + 1; j < nx; ++j)
                    if (xs[j] < xs[i]) { const i32 t = xs[i]; xs[i] = xs[j]; xs[j] = t; }

            for (u32 i = 0; i + 1 < nx; i += 2)
            {
                // Horizontal span from xs[i] to xs[i+1] in 8.8 space.
                i32 xa = xs[i];
                i32 xb = xs[i + 1];
                if (xb <= xa) continue;

                // Pixel range affected.
                i32 pa = (xa - origin_x + 256 - 1) >> 8;   // ceil((xa - origin_x)/256)
                i32 pb = (xb - origin_x) >> 8;             // floor
                if (pa < 0) pa = 0;
                if (pb >= static_cast<i32>(width)) pb = static_cast<i32>(width) - 1;

                for (i32 px = pa; px <= pb; ++px)
                {
                    const i32 cx0 = origin_x + (px << 8);
                    const i32 cx1 = cx0 + 256;
                    const i32 l = (xa > cx0) ? xa : cx0;
                    const i32 r = (xb < cx1) ? xb : cx1;
                    if (r > l) row_sums[px] += (r - l) * 255 / 256;
                }
            }
        }

        // Average coverage over sub-scanlines and store.
        for (u32 px = 0; px < width; ++px)
        {
            const i32 cov = row_sums[px] / static_cast<i32>(kSubY);
            out[static_cast<u32>(row) * width + px] =
                static_cast<u8>((cov > 255) ? 255 : ((cov < 0) ? 0 : cov));
        }
    }
}

// ---------------------------------------------------------------------------
// Cache operations.
// ---------------------------------------------------------------------------
Glyph* cache_lookup(Face* f, u32 cp, u32 px) noexcept
{
    for (u32 i = 0; i < f->cache_count; ++i)
    {
        if (f->cache[i].codepoint == cp && f->cache[i].pixel_size == px)
        {
            f->cache[i].age = ++f->cache_age;
            ++g_hits;
            return &f->cache[i].g;
        }
    }
    ++g_misses;
    return nullptr;
}

void cache_insert(Face* f, u32 cp, u32 px, const Glyph& g) noexcept
{
    if (f->cache_count < kMaxGlyphCacheEntries)
    {
        CacheEntry& e = f->cache[f->cache_count++];
        e.codepoint = cp;
        e.pixel_size = px;
        e.age = ++f->cache_age;
        e.g = g;
        return;
    }
    // Evict the oldest entry.
    u32 oldest = 0;
    for (u32 i = 1; i < f->cache_count; ++i)
        if (f->cache[i].age < f->cache[oldest].age) oldest = i;
    if (f->cache[oldest].g.coverage)
        mm::Heap::deallocate(f->cache[oldest].g.coverage);
    CacheEntry& e = f->cache[oldest];
    e.codepoint = cp;
    e.pixel_size = px;
    e.age = ++f->cache_age;
    e.g = g;
}

// Rasterize one codepoint at one pixel size. Returns the glyph (freshly
// allocated). Caller owns the coverage buffer and inserts into cache.
Glyph rasterize_glyph(Face* f, u32 cp, u32 px) noexcept
{
    Glyph g{};
    const u32 gi = cmap_lookup(f, cp);
    if (gi == 0)
    {
        // .notdef: return a blank with advance.
        g.advance = 0;
        return g;
    }

    // Advance in pixels.
    const i32 adv_units = hmtx_advance(f, gi);
    g.advance = static_cast<i32>((static_cast<i64>(adv_units) * px) / f->units_per_em);

    if (!f->off_glyf || !f->off_loca) return g;

    // Locate glyph.
    u32 glyph_off;
    u32 glyph_len;
    if (f->loca_format == 0)
    {
        const u8* loca = f->data + f->off_loca;
        glyph_off = static_cast<u32>(rd16(loca + gi * 2)) * 2u;
        glyph_len = static_cast<u32>(rd16(loca + (gi + 1) * 2)) * 2u - glyph_off;
    }
    else
    {
        const u8* loca = f->data + f->off_loca;
        glyph_off = rd32(loca + gi * 4);
        glyph_len = rd32(loca + (gi + 1) * 4) - glyph_off;
    }
    if (glyph_len == 0) return g;   // space

    Outline outline{};
    if (!parse_simple_glyph(f, f->off_glyf + glyph_off, outline)) return g;
    if (outline.point_count == 0) return g;

    // Scale: font units -> 8.8 pixels. scale_num = px * 256; scale_den = units_per_em.
    const i32 scale_num = static_cast<i32>(px) * 256;
    const i32 scale_den = static_cast<i32>(f->units_per_em);

    const i32 ox = (outline.x_min * scale_num) / scale_den;
    const i32 oy = (outline.y_min * scale_num) / scale_den;
    const i32 ex = (outline.x_max * scale_num) / scale_den;
    const i32 ey = (outline.y_max * scale_num) / scale_den;

    const i32 w = ((ex - ox) + 255) >> 8;
    const i32 h = ((ey - oy) + 255) >> 8;

    g.bearing_x = ox >> 8;
    g.bearing_y = ey >> 8;   // top of the glyph above baseline
    g.width = (w > 0) ? w : 0;
    g.height = (h > 0) ? h : 0;

    if (g.width == 0 || g.height == 0)
    {
        g.coverage = nullptr;
        return g;
    }
    if (g.width > 256 || g.height > 256) return g;   // refuse absurd sizes

    constexpr u32 kMaxEdges = 512;
    Edge edges[kMaxEdges];
    u32 edge_count = 0;
    i32 ymin = 0, ymax = 0;
    flatten_contours(outline, scale_num, scale_den,
                     edges, edge_count, ymin, ymax, kMaxEdges);
    if (edge_count == 0) return g;

    const usize n = static_cast<usize>(g.width) * static_cast<usize>(g.height);
    g.coverage = static_cast<u8*>(mm::Heap::allocate(n));
    if (!g.coverage)
    {
        g.width = g.height = 0;
        return g;
    }
    rasterize_edges(edges, edge_count, ymin, ymax,
                    ox, oy,
                    static_cast<u32>(g.width), static_cast<u32>(g.height),
                    g.coverage);

    return g;
}

// ---------------------------------------------------------------------------
// UTF-8 helpers.
// ---------------------------------------------------------------------------
u32 utf8_next(const char*& s) noexcept
{
    const u8 c0 = static_cast<u8>(*s);
    if (c0 == 0) return 0;
    if (c0 < 0x80u) { ++s; return c0; }
    if ((c0 & 0xE0u) == 0xC0u && s[1])
    {
        const u32 cp = ((c0 & 0x1Fu) << 6) | (static_cast<u8>(s[1]) & 0x3Fu);
        s += 2; return cp;
    }
    if ((c0 & 0xF0u) == 0xE0u && s[1] && s[2])
    {
        const u32 cp = ((c0 & 0x0Fu) << 12)
                     | ((static_cast<u8>(s[1]) & 0x3Fu) << 6)
                     |  (static_cast<u8>(s[2]) & 0x3Fu);
        s += 3; return cp;
    }
    if ((c0 & 0xF8u) == 0xF0u && s[1] && s[2] && s[3])
    {
        const u32 cp = ((c0 & 0x07u) << 18)
                     | ((static_cast<u8>(s[1]) & 0x3Fu) << 12)
                     | ((static_cast<u8>(s[2]) & 0x3Fu) << 6)
                     |  (static_cast<u8>(s[3]) & 0x3Fu);
        s += 4; return cp;
    }
    ++s;
    return '?';
}

} // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

Face* load(const char* vfs_path) noexcept
{
    if (!vfs_path) return nullptr;

    auto* vn = fs::vfs_lookup(vfs_path, "/");
    if (!vn || !vn->ops || !vn->ops->size || !vn->ops->read)
    {
        log::write(log::Level::Warn, "font", "%s: not in VFS", vfs_path);
        return nullptr;
    }
    const isize sz = vn->ops->size(vn);
    if (sz <= 0 || sz > 16 * 1024 * 1024)
    {
        log::write(log::Level::Warn, "font", "%s: bad size %lld",
                   vfs_path, static_cast<long long>(sz));
        return nullptr;
    }

    auto* data = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz)));
    if (!data) return nullptr;
    isize got = 0;
    while (got < sz)
    {
        const isize n = vn->ops->read(vn, data + got, static_cast<usize>(got),
                                      static_cast<usize>(sz - got));
        if (n <= 0) break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(data);
        log::write(log::Level::Warn, "font", "%s: short read", vfs_path);
        return nullptr;
    }

    // Validate sfnt version.
    const u32 version = rd32(data);
    if (version != 0x00010000u && version != 0x4F54544Fu)   // 1.0 or OTTO
    {
        mm::Heap::deallocate(data);
        log::write(log::Level::Warn, "font", "%s: not a TTF/OTF (magic 0x%llx)",
                   vfs_path, static_cast<unsigned long long>(version));
        return nullptr;
    }

    auto* f = static_cast<Face*>(mm::Heap::allocate(sizeof(Face)));
    if (!f) { mm::Heap::deallocate(data); return nullptr; }
    libk::memset(f, 0, sizeof(Face));
    f->data = data;
    f->size = static_cast<usize>(sz);

    f->off_head = find_table(f, 0x68656164u);   // "head"
    f->off_hhea = find_table(f, 0x68686561u);   // "hhea"
    f->off_hmtx = find_table(f, 0x686D7478u);   // "hmtx"
    f->off_maxp = find_table(f, 0x6D617870u);   // "maxp"
    f->off_cmap = find_table(f, 0x636D6170u);   // "cmap"
    f->off_loca = find_table(f, 0x6C6F6361u);   // "loca"
    f->off_glyf = find_table(f, 0x676C7966u);   // "glyf"
    f->off_os2  = find_table(f, 0x4F532F32u);   // "OS/2"

    if (!f->off_head || !f->off_hhea || !f->off_maxp || !f->off_cmap ||
        !f->off_loca || !f->off_glyf)
    {
        log::write(log::Level::Warn, "font",
                   "%s: missing required tables "
                   "(head=%llu hhea=%llu maxp=%llu cmap=%llu loca=%llu glyf=%llu)",
                   vfs_path,
                   static_cast<unsigned long long>(f->off_head),
                   static_cast<unsigned long long>(f->off_hhea),
                   static_cast<unsigned long long>(f->off_maxp),
                   static_cast<unsigned long long>(f->off_cmap),
                   static_cast<unsigned long long>(f->off_loca),
                   static_cast<unsigned long long>(f->off_glyf));
        unload(f);
        return nullptr;
    }

    // head.
    f->units_per_em = rd16(f->data + f->off_head + 18);
    f->loca_format = static_cast<u16>(rdi16(f->data + f->off_head + 50));
    if (f->units_per_em == 0) f->units_per_em = 1000;   // fallback

    // hhea.
    f->ascent_units  = rdi16(f->data + f->off_hhea + 4);
    f->descent_units = rdi16(f->data + f->off_hhea + 6);
    f->line_gap_units = rdi16(f->data + f->off_hhea + 8);
    f->num_h_metrics = rd16(f->data + f->off_hhea + 34);

    // maxp.
    f->num_glyphs = rd16(f->data + f->off_maxp + 4);

    ++g_faces_loaded;
    log::write(log::Level::Info, "font",
               "loaded %s: %llu glyphs, upem=%llu loca=%s",
               vfs_path,
               static_cast<unsigned long long>(f->num_glyphs),
               static_cast<unsigned long long>(f->units_per_em),
               (f->loca_format == 0) ? "short" : "long");

    return f;
}

void unload(Face* f) noexcept
{
    if (!f) return;
    for (u32 i = 0; i < f->cache_count; ++i)
        if (f->cache[i].g.coverage)
            mm::Heap::deallocate(f->cache[i].g.coverage);
    if (f->scratch)
        mm::Heap::deallocate(f->scratch);
    if (f->data)
        mm::Heap::deallocate(f->data);
    if (g_default == f) g_default = nullptr;
    mm::Heap::deallocate(f);
}

Face* default_face() noexcept { return g_default; }
void  set_default_face(Face* f) noexcept { g_default = f; }

Metrics metrics(Face* f, u32 px) noexcept
{
    Metrics m{};
    if (!f) return m;
    const i32 upem = static_cast<i32>(f->units_per_em);
    m.ascent  = (static_cast<i32>(f->ascent_units)  * static_cast<i32>(px)) / upem;
    m.descent = (-static_cast<i32>(f->descent_units) * static_cast<i32>(px)) / upem;
    if (m.descent < 0) m.descent = -m.descent;
    m.line_gap = (static_cast<i32>(f->line_gap_units) * static_cast<i32>(px)) / upem;
    if (m.line_gap < 0) m.line_gap = 0;
    m.line_height = m.ascent + m.descent + m.line_gap;
    return m;
}

i32 advance(Face* f, u32 cp, u32 px) noexcept
{
    if (!f) return 0;
    const u32 gi = cmap_lookup(f, cp);
    if (gi == 0) return static_cast<i32>(px) / 2;
    const i32 adv_units = hmtx_advance(f, gi);
    return static_cast<i32>((static_cast<i64>(adv_units) * px) / f->units_per_em);
}

i32 text_width(Face* f, const char* utf8, u32 px) noexcept
{
    if (!f || !utf8) return 0;
    i32 w = 0;
    const char* p = utf8;
    for (;;)
    {
        const u32 cp = utf8_next(p);
        if (cp == 0) break;
        w += advance(f, cp, px);
    }
    return w;
}

const Glyph* glyph(Face* f, u32 cp, u32 px) noexcept
{
    if (!f) return nullptr;

    // Clamp px to a reasonable range.
    if (px < 6) px = 6;
    if (px > 96) px = 96;

    if (Glyph* cached = cache_lookup(f, cp, px))
        return cached;

    Glyph g = rasterize_glyph(f, cp, px);
    cache_insert(f, cp, px, g);

    // Return the freshly inserted cache entry (or the last inserted if
    // eviction happened -- either way, the tail is what we want).
    if (f->cache_count == 0) return nullptr;
    return &f->cache[f->cache_count - 1].g;
}

void blend_glyph(u32* pixels, u32 pitch, u32 surf_w, u32 surf_h,
                 const Glyph* g, i32 pen_x, i32 pen_y, u32 color) noexcept
{
    if (!pixels || !g || !g->coverage) return;

    const i32 ox = pen_x + g->bearing_x;
    const i32 oy = pen_y - g->bearing_y;

    const u32 sr = (color >> 16) & 0xFFu;
    const u32 sg = (color >>  8) & 0xFFu;
    const u32 sb = (color      ) & 0xFFu;

    for (i32 j = 0; j < g->height; ++j)
    {
        const i32 py = oy + j;
        if (py < 0 || py >= static_cast<i32>(surf_h)) continue;
        const u8* row = g->coverage + static_cast<usize>(j) * static_cast<usize>(g->width);
        u32* dst_row = pixels + static_cast<usize>(py) * pitch;

        for (i32 i = 0; i < g->width; ++i)
        {
            const i32 px = ox + i;
            if (px < 0 || px >= static_cast<i32>(surf_w)) continue;
            const u32 a = row[i];
            if (a == 0) continue;

            const u32 bg = dst_row[px];
            const u32 br = (bg >> 16) & 0xFFu;
            const u32 bgc = (bg >>  8) & 0xFFu;
            const u32 bb = (bg      ) & 0xFFu;

            const u32 nr = (sr * a + br * (255u - a)) / 255u;
            const u32 ng = (sg * a + bgc * (255u - a)) / 255u;
            const u32 nb = (sb * a + bb * (255u - a)) / 255u;

            dst_row[px] = (nr << 16) | (ng << 8) | nb;
        }
    }
}

i32 draw_text(u32* pixels, u32 pitch, u32 surf_w, u32 surf_h,
              Face* f, i32 x, i32 y, const char* utf8,
              u32 px, u32 color) noexcept
{
    if (!f || !utf8) return 0;
    i32 pen = x;
    const char* p = utf8;
    for (;;)
    {
        const u32 cp = utf8_next(p);
        if (cp == 0) break;
        const Glyph* g = glyph(f, cp, px);
        if (g && g->coverage)
            blend_glyph(pixels, pitch, surf_w, surf_h, g, pen, y, color);
        pen += advance(f, cp, px);
    }
    return pen - x;
}

u64 faces_loaded() noexcept { return g_faces_loaded; }
u64 glyphs_cached(Face* f) noexcept { return f ? f->cache_count : 0; }
u64 cache_hits() noexcept { return g_hits; }
u64 cache_misses() noexcept { return g_misses; }

} // namespace notyvos::font
