#include <kernel/img/svg.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img::svg
{

namespace
{

// ---------------------------------------------------------------------------
// Small parsing helpers. All coordinates are 8.8 fixed-point.
// ---------------------------------------------------------------------------
inline bool is_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
inline bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}
inline char lc(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

inline const char* skip_ws(const char* p) noexcept
{
    while (*p && (is_space(*p) || *p == ','))
        ++p;
    return p;
}

// Parse a decimal number; returns *256 (fixed point 8.8). Advances `p`.
i32 parse_fx(const char*& p) noexcept
{
    p = skip_ws(p);
    bool neg = false;
    if (*p == '-')
    {
        neg = true;
        ++p;
    }
    else if (*p == '+')
        ++p;

    u32 ip = 0;
    while (is_digit(*p))
    {
        ip = ip * 10u + static_cast<u32>(*p - '0');
        ++p;
    }
    u32 fp = 0, fs = 1;
    if (*p == '.')
    {
        ++p;
        while (is_digit(*p))
        {
            fp = fp * 10u + static_cast<u32>(*p - '0');
            fs *= 10u;
            ++p;
        }
    }
    // Ignore exponent (rare in icons).
    if (*p == 'e' || *p == 'E')
    {
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        while (is_digit(*p))
            ++p;
    }

    i32 v = static_cast<i32>(ip * 256u + (fp * 256u) / (fs ? fs : 1u));
    return neg ? -v : v;
}

u32 parse_color(const char* v) noexcept
{
    if (!v || !*v)
        return 0x000000u;
    if (lc(v[0]) == 'n' && lc(v[1]) == 'o' && lc(v[2]) == 'n' && lc(v[3]) == 'e')
        return 0xFFFFFFFFu; // sentinel = skip

    if (v[0] == '#')
    {
        auto hexv = [](char c) -> u32
        {
            if (c >= '0' && c <= '9')
                return static_cast<u32>(c - '0');
            const char l = lc(c);
            if (l >= 'a' && l <= 'f')
                return static_cast<u32>(l - 'a' + 10);
            return 0;
        };
        const usize n = libk::strlen(v + 1);
        if (n >= 6)
            return (hexv(v[1]) << 20) | (hexv(v[2]) << 16) | (hexv(v[3]) << 12) |
                   (hexv(v[4]) << 8) | (hexv(v[5]) << 4) | hexv(v[6]);
        if (n >= 3)
            return ((hexv(v[1]) * 17u) << 16) | ((hexv(v[2]) * 17u) << 8) | (hexv(v[3]) * 17u);
        return 0;
    }

    struct Named
    {
        const char* n;
        u32 c;
    };
    static const Named kNames[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF},   {"red", 0xFF0000},     {"green", 0x008000},
        {"lime", 0x00FF00},  {"blue", 0x0000FF},    {"yellow", 0xFFFF00},  {"cyan", 0x00FFFF},
        {"aqua", 0x00FFFF},  {"magenta", 0xFF00FF}, {"fuchsia", 0xFF00FF}, {"gray", 0x808080},
        {"grey", 0x808080},  {"silver", 0xC0C0C0},  {"maroon", 0x800000},  {"olive", 0x808000},
        {"navy", 0x000080},  {"teal", 0x008080},    {"purple", 0x800080},  {"orange", 0xFFA500},
        {"gold", 0xFFD700},  {"pink", 0xFFC0CB},
    };
    char buf[16];
    usize i = 0;
    while (v[i] && i < sizeof(buf) - 1)
    {
        buf[i] = lc(v[i]);
        ++i;
    }
    buf[i] = 0;
    for (const auto& e : kNames)
        if (libk::strcmp(buf, e.n) == 0)
            return e.c;
    return 0;
}

// Attribute lookup. Copies the value into `out`.
bool attr(const char* tag, const char* name, char* out, usize cap) noexcept
{
    const usize nlen = libk::strlen(name);
    const char* p = tag;
    while (*p)
    {
        // Skip until whitespace.
        while (*p && !is_space(*p) && *p != '>')
            ++p;
        p = skip_ws(p);
        if (!*p || *p == '>' || *p == '/')
            return false;
        const char* nm = p;
        while (*p && *p != '=' && *p != '>' && !is_space(*p))
            ++p;
        const usize this_len = static_cast<usize>(p - nm);
        const bool match = (this_len == nlen) && (libk::strncmp(nm, name, nlen) == 0);
        p = skip_ws(p);
        if (*p != '=')
            continue;
        ++p;
        p = skip_ws(p);
        char quote = 0;
        if (*p == '"' || *p == '\'')
        {
            quote = *p++;
        }
        const char* vs = p;
        if (quote)
        {
            while (*p && *p != quote)
                ++p;
        }
        else
        {
            while (*p && !is_space(*p) && *p != '>' && *p != '/')
                ++p;
        }
        if (match)
        {
            const usize vl = static_cast<usize>(p - vs);
            const usize copy = (vl < cap - 1) ? vl : cap - 1;
            for (usize i = 0; i < copy; ++i)
                out[i] = vs[i];
            out[copy] = 0;
            return true;
        }
        if (quote && *p == quote)
            ++p;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Raster state.
// ---------------------------------------------------------------------------
struct BitmapCtx
{
    u32* pix;
    u32 w, h;
    u32 fill;
};

inline void plot(BitmapCtx& bc, i32 x, i32 y) noexcept
{
    if (x < 0 || y < 0)
        return;
    if (static_cast<u32>(x) >= bc.w || static_cast<u32>(y) >= bc.h)
        return;
    bc.pix[static_cast<usize>(y) * bc.w + static_cast<u32>(x)] = bc.fill;
}

struct Pt
{
    i32 x, y;
}; // 8.8 fixed point

// Even-odd scanline polygon fill.
void fill_poly(BitmapCtx& bc, const Pt* pts, u32 n) noexcept
{
    if (n < 3)
        return;
    i32 ymin = pts[0].y, ymax = pts[0].y;
    for (u32 i = 1; i < n; ++i)
    {
        if (pts[i].y < ymin)
            ymin = pts[i].y;
        if (pts[i].y > ymax)
            ymax = pts[i].y;
    }
    const i32 sy0 = ymin >> 8;
    const i32 sy1 = (ymax + 255) >> 8;
    for (i32 sy = sy0; sy <= sy1; ++sy)
    {
        if (sy < 0)
            continue;
        if (static_cast<u32>(sy) >= bc.h)
            break;
        const i32 scan = (sy << 8) | 128;

        i32 xs[48];
        u32 nx = 0;
        for (u32 i = 0; i < n; ++i)
        {
            const Pt& a = pts[i];
            const Pt& b = pts[(i + 1) % n];
            if (a.y == b.y)
                continue;
            const i32 ytop = (a.y < b.y) ? a.y : b.y;
            const i32 ybot = (a.y < b.y) ? b.y : a.y;
            if (scan < ytop || scan >= ybot)
                continue;
            const i32 dy = b.y - a.y;
            const i32 x = a.x + static_cast<i32>((static_cast<i64>(b.x - a.x) * (scan - a.y)) / dy);
            if (nx < 48)
                xs[nx++] = x;
        }
        for (u32 i = 0; i < nx; ++i)
            for (u32 j = i + 1; j < nx; ++j)
                if (xs[j] < xs[i])
                {
                    const i32 t = xs[i];
                    xs[i] = xs[j];
                    xs[j] = t;
                }
        for (u32 i = 0; i + 1 < nx; i += 2)
        {
            const i32 px0 = xs[i] >> 8;
            const i32 px1 = (xs[i + 1] + 255) >> 8;
            for (i32 px = px0; px < px1; ++px)
                plot(bc, px, sy);
        }
    }
}

void fill_rect_px(BitmapCtx& bc, i32 x, i32 y, i32 w, i32 h) noexcept
{
    if (w <= 0 || h <= 0)
        return;
    for (i32 j = 0; j < h; ++j)
        for (i32 i = 0; i < w; ++i)
            plot(bc, x + i, y + j);
}

void fill_circle_px(BitmapCtx& bc, i32 cx, i32 cy, i32 r) noexcept
{
    if (r <= 0)
        return;
    for (i32 j = -r; j <= r; ++j)
    {
        i32 span = 0;
        const i32 rem = r * r - j * j;
        while ((span + 1) * (span + 1) <= rem)
            ++span;
        for (i32 i = -span; i <= span; ++i)
            plot(bc, cx + i, cy + j);
    }
}

// Subdivide a cubic Bézier into line segments. `emit` receives the endpoint.
template<typename Emit>
void cubic_to_lines(Pt p0, Pt p1, Pt p2, Pt p3, u32 depth, Emit emit) noexcept
{
    // Flatness test using the perpendicular distance of control points.
    auto perp = [&](const Pt& p) -> i32
    {
        const i64 dx = p3.x - p0.x, dy = p3.y - p0.y;
        const i64 num = dy * p.x - dx * p.y + dx * p0.y - dy * p0.x;
        const i64 den2 = dx * dx + dy * dy;
        if (den2 == 0)
            return 0;
        return static_cast<i32>((num < 0 ? -num : num) / den2);
    };
    if (depth >= 6 || (perp(p1) < 4 && perp(p2) < 4))
    {
        emit(p3);
        return;
    }
    const Pt m01 = {(p0.x + p1.x) / 2, (p0.y + p1.y) / 2};
    const Pt m12 = {(p1.x + p2.x) / 2, (p1.y + p2.y) / 2};
    const Pt m23 = {(p2.x + p3.x) / 2, (p2.y + p3.y) / 2};
    const Pt m012 = {(m01.x + m12.x) / 2, (m01.y + m12.y) / 2};
    const Pt m123 = {(m12.x + m23.x) / 2, (m12.y + m23.y) / 2};
    const Pt mm = {(m012.x + m123.x) / 2, (m012.y + m123.y) / 2};
    cubic_to_lines(p0, m01, m012, mm, depth + 1, emit);
    cubic_to_lines(mm, m123, m23, p3, depth + 1, emit);
}

// ---------------------------------------------------------------------------
// Path parsing.
// ---------------------------------------------------------------------------
// Scale from viewBox to output pixels.
struct Scale
{
    i32 vx, vy, vw, vh;
    i32 ow, oh;
    Pt apply(i32 x_fx, i32 y_fx) const noexcept
    {
        const i64 sx = static_cast<i64>(x_fx - vx) * (static_cast<i64>(ow) * 256) / vw;
        const i64 sy = static_cast<i64>(y_fx - vy) * (static_cast<i64>(oh) * 256) / vh;
        return {static_cast<i32>(sx), static_cast<i32>(sy)};
    }
};

void raster_path(BitmapCtx& bc, const Scale& sc, const char* d) noexcept
{
    Pt pts[512];
    u32 n = 0;
    Pt cur = {0, 0};
    Pt start = {0, 0};
    Pt cur_emit = sc.apply(0, 0);
    char cmd = 0;

    const char* p = d;
    while (*p)
    {
        p = skip_ws(p);
        if (!*p)
            break;

        const char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        {
            cmd = c;
            ++p;
        }
        else if (cmd == 0)
        {
            ++p;
            continue;
        }
        else if (cmd == 'M')
            cmd = 'L';
        else if (cmd == 'm')
            cmd = 'l';

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        const char uc = rel ? static_cast<char>(cmd - 32) : cmd;

        auto num = [&]() -> i32 { return parse_fx(p); };

        switch (uc)
        {
        case 'M':
        {
            const i32 x = num(), y = num();
            Pt np = {rel ? cur.x + x : x, rel ? cur.y + y : y};
            if (n > 0)
            {
                fill_poly(bc, pts, n);
                n = 0;
            }
            cur = np;
            start = np;
            cur_emit = sc.apply(cur.x, cur.y);
            pts[n++] = cur_emit;
            break;
        }
        case 'L':
        {
            const i32 x = num(), y = num();
            cur = {rel ? cur.x + x : x, rel ? cur.y + y : y};
            cur_emit = sc.apply(cur.x, cur.y);
            if (n < 512)
                pts[n++] = cur_emit;
            break;
        }
        case 'H':
        {
            const i32 x = num();
            cur.x = rel ? cur.x + x : x;
            cur_emit = sc.apply(cur.x, cur.y);
            if (n < 512)
                pts[n++] = cur_emit;
            break;
        }
        case 'V':
        {
            const i32 y = num();
            cur.y = rel ? cur.y + y : y;
            cur_emit = sc.apply(cur.x, cur.y);
            if (n < 512)
                pts[n++] = cur_emit;
            break;
        }
        case 'C':
        {
            const i32 x1 = num(), y1 = num(), x2 = num(), y2 = num(), x = num(), y = num();
            const Pt c1 = {rel ? cur.x + x1 : x1, rel ? cur.y + y1 : y1};
            const Pt c2 = {rel ? cur.x + x2 : x2, rel ? cur.y + y2 : y2};
            const Pt ep = {rel ? cur.x + x : x, rel ? cur.y + y : y};
            const Pt c1e = sc.apply(c1.x, c1.y);
            const Pt c2e = sc.apply(c2.x, c2.y);
            const Pt epe = sc.apply(ep.x, ep.y);
            cubic_to_lines(cur_emit, c1e, c2e, epe, 0,
                           [&](Pt pt)
                           {
                               if (n < 512)
                                   pts[n++] = pt;
                           });
            cur = ep;
            cur_emit = epe;
            break;
        }
        case 'Q':
        {
            const i32 x1 = num(), y1 = num(), x = num(), y = num();
            const Pt cp = {rel ? cur.x + x1 : x1, rel ? cur.y + y1 : y1};
            const Pt ep = {rel ? cur.x + x : x, rel ? cur.y + y : y};
            const Pt cpe = sc.apply(cp.x, cp.y);
            const Pt epe = sc.apply(ep.x, ep.y);
            // Convert to cubic.
            const Pt c1 = {(cur_emit.x + 2 * cpe.x) / 3, (cur_emit.y + 2 * cpe.y) / 3};
            const Pt c2 = {(epe.x + 2 * cpe.x) / 3, (epe.y + 2 * cpe.y) / 3};
            cubic_to_lines(cur_emit, c1, c2, epe, 0,
                           [&](Pt pt)
                           {
                               if (n < 512)
                                   pts[n++] = pt;
                           });
            cur = ep;
            cur_emit = epe;
            break;
        }
        case 'Z':
        {
            const Pt se = sc.apply(start.x, start.y);
            if (n > 0 && n < 512)
                pts[n++] = se;
            fill_poly(bc, pts, n);
            n = 0;
            cur = start;
            cur_emit = se;
            break;
        }
        default:
            // Unsupported command. Abort the rest of this path.
            if (n > 0)
                fill_poly(bc, pts, n);
            return;
        }
    }
    if (n > 0)
        fill_poly(bc, pts, n);
}

// ---------------------------------------------------------------------------
// Tag scanning.
// ---------------------------------------------------------------------------
bool scan_tag(const char*& p, char* name, usize cap, char* body, usize body_cap) noexcept
{
    while (*p && *p != '<')
        ++p;
    if (!*p)
        return false;
    ++p;
    if (*p == '/' || *p == '?' || *p == '!')
    {
        ++p;
        return false;
    }

    usize ni = 0;
    while (*p && !is_space(*p) && *p != '>' && *p != '/' && ni < cap - 1)
        name[ni++] = *p++;
    name[ni] = 0;

    usize bi = 0;
    while (*p && *p != '>' && bi < body_cap - 1)
        body[bi++] = *p++;
    body[bi] = 0;
    if (*p == '>')
        ++p;
    return true;
}

} // namespace

bool rasterize(const char* svg_src, usize svg_len, u32 width, u32 height, Bitmap& out) noexcept
{
    out = {0, 0, nullptr, false};
    if (!svg_src || svg_len == 0 || width == 0 || height == 0)
        return false;
    if (width > 512 || height > 512)
        return false;

    const usize n_pixels = static_cast<usize>(width) * height;
    auto* pix = static_cast<u32*>(mm::Heap::allocate(n_pixels * sizeof(u32)));
    if (!pix)
        return false;
    for (usize i = 0; i < n_pixels; ++i)
        pix[i] = 0x00000000u;

    // ---- Pass 1: find the viewBox ----
    Scale sc{0,
             0,
             static_cast<i32>(width) * 256,
             static_cast<i32>(height) * 256,
             static_cast<i32>(width),
             static_cast<i32>(height)};
    {
        const char* p = svg_src;
        char name[24];
        char body[512];
        while (*p)
        {
            if (!scan_tag(p, name, sizeof(name), body, sizeof(body)))
            {
                if (!*p)
                    break;
                continue;
            }
            if (libk::strcmp(name, "svg") == 0)
            {
                char vb[128];
                if (attr(body, "viewBox", vb, sizeof(vb)))
                {
                    const char* q = vb;
                    sc.vx = parse_fx(q);
                    sc.vy = parse_fx(q);
                    sc.vw = parse_fx(q);
                    sc.vh = parse_fx(q);
                    if (sc.vw <= 0)
                        sc.vw = static_cast<i32>(width) * 256;
                    if (sc.vh <= 0)
                        sc.vh = static_cast<i32>(height) * 256;
                }
                break;
            }
        }
    }

    BitmapCtx bc{pix, width, height, 0};

    // ---- Pass 2: rasterize every shape ----
    const char* p = svg_src;
    char name[24];
    char body[1024];
    while (*p)
    {
        if (!scan_tag(p, name, sizeof(name), body, sizeof(body)))
        {
            if (!*p)
                break;
            continue;
        }

        // Skip the outer <svg>.
        if (libk::strcmp(name, "svg") == 0)
            continue;

        char fill[64];
        if (!attr(body, "fill", fill, sizeof(fill)))
            libk::strcpy(fill, "black");
        const u32 f = parse_color(fill);
        if (f == 0xFFFFFFFFu)
            continue; // none
        bc.fill = f;

        if (libk::strcmp(name, "path") == 0)
        {
            char d[1024];
            if (attr(body, "d", d, sizeof(d)))
                raster_path(bc, sc, d);
        }
        else if (libk::strcmp(name, "rect") == 0)
        {
            char b[32];
            i32 x = 0, y = 0, w = 0, h = 0;
            if (attr(body, "x", b, sizeof(b)))
            {
                const char* q = b;
                x = parse_fx(q);
            }
            if (attr(body, "y", b, sizeof(b)))
            {
                const char* q = b;
                y = parse_fx(q);
            }
            if (attr(body, "width", b, sizeof(b)))
            {
                const char* q = b;
                w = parse_fx(q);
            }
            if (attr(body, "height", b, sizeof(b)))
            {
                const char* q = b;
                h = parse_fx(q);
            }
            const Pt p0 = sc.apply(x, y);
            const Pt p1 = sc.apply(x + w, y + h);
            fill_rect_px(bc, p0.x >> 8, p0.y >> 8, (p1.x - p0.x) >> 8, (p1.y - p0.y) >> 8);
        }
        else if (libk::strcmp(name, "circle") == 0)
        {
            char b[32];
            i32 cx = 0, cy = 0, r = 0;
            if (attr(body, "cx", b, sizeof(b)))
            {
                const char* q = b;
                cx = parse_fx(q);
            }
            if (attr(body, "cy", b, sizeof(b)))
            {
                const char* q = b;
                cy = parse_fx(q);
            }
            if (attr(body, "r", b, sizeof(b)))
            {
                const char* q = b;
                r = parse_fx(q);
            }
            const Pt ce = sc.apply(cx, cy);
            const i64 rx = static_cast<i64>(r) * sc.ow * 256 / sc.vw;
            const i64 ry = static_cast<i64>(r) * sc.oh * 256 / sc.vh;
            fill_circle_px(bc, ce.x >> 8, ce.y >> 8, static_cast<i32>((rx < ry ? rx : ry) >> 8));
        }
        else if (libk::strcmp(name, "polygon") == 0 || libk::strcmp(name, "polyline") == 0)
        {
            char pts[768];
            if (attr(body, "points", pts, sizeof(pts)))
            {
                Pt sp[256];
                u32 n = 0;
                const char* q = pts;
                while (*q && n < 256)
                {
                    const i32 x = parse_fx(q);
                    const i32 y = parse_fx(q);
                    sp[n++] = sc.apply(x, y);
                    const char* t = skip_ws(q);
                    if (*t == 0)
                        break;
                    if (*t == 'z' || *t == 'Z')
                    {
                        q = t + 1;
                        break;
                    }
                    q = t;
                }
                if (n >= 3)
                    fill_poly(bc, sp, n);
            }
        }
        else if (libk::strcmp(name, "line") == 0)
        {
            char b[32];
            i32 x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (attr(body, "x1", b, sizeof(b)))
            {
                const char* q = b;
                x1 = parse_fx(q);
            }
            if (attr(body, "y1", b, sizeof(b)))
            {
                const char* q = b;
                y1 = parse_fx(q);
            }
            if (attr(body, "x2", b, sizeof(b)))
            {
                const char* q = b;
                x2 = parse_fx(q);
            }
            if (attr(body, "y2", b, sizeof(b)))
            {
                const char* q = b;
                y2 = parse_fx(q);
            }
            const Pt a = sc.apply(x1, y1);
            const Pt b2 = sc.apply(x2, y2);
            const Pt quad[4] = {a, a, b2, b2};
            fill_poly(bc, quad, 4);
        }
        // Ignore unsupported elements silently.
    }

    out.width = width;
    out.height = height;
    out.pixels = pix;
    out.owned = true;
    return true;
}

void free(Bitmap& bmp) noexcept
{
    if (bmp.pixels && bmp.owned)
        mm::Heap::deallocate(bmp.pixels);
    bmp = {0, 0, nullptr, false};
}

} // namespace notyvos::img::svg
