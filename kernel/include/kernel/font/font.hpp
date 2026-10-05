#pragma once
#include <kernel/types.hpp>

namespace notyvos::font
{

// ---------------------------------------------------------------------------
// TrueType font subsystem.
//
// Parses TTF/OTF files, rasterizes glyphs on demand, and caches coverage
// bitmaps. All rendering is anti-aliased.
// ---------------------------------------------------------------------------

struct Face;

// A rasterized glyph: an 8-bit coverage bitmap plus metrics. Coverage is
// `width * height` bytes, row stride = width, 0 = transparent, 255 = opaque.
// `bearing_x` / `bearing_y` place the bitmap relative to the pen position:
//   top-left in surface space = (pen_x + bearing_x, pen_y - bearing_y)
struct Glyph
{
    i32 width;
    i32 height;
    i32 bearing_x;
    i32 bearing_y;
    i32 advance;
    u8* coverage;
};

// Load a TTF/OTF file from the VFS. Returns nullptr on failure.
Face* load(const char* vfs_path) noexcept;

// Free all resources for this face.
void unload(Face* face) noexcept;

// The face used when callers do not specify one explicitly.
Face* default_face() noexcept;
void set_default_face(Face* face) noexcept;

struct Metrics
{
    i32 ascent;
    i32 descent;
    i32 line_gap;
    i32 line_height;
};
Metrics metrics(Face* face, u32 pixel_size) noexcept;

i32 advance(Face* face, u32 codepoint, u32 pixel_size) noexcept;
i32 text_width(Face* face, const char* utf8, u32 pixel_size) noexcept;

const Glyph* glyph(Face* face, u32 codepoint, u32 pixel_size) noexcept;

i32 draw_text(u32* pixels, u32 pitch, u32 surf_w, u32 surf_h, Face* face, i32 x, i32 y,
              const char* utf8, u32 pixel_size, u32 color) noexcept;

void blend_glyph(u32* pixels, u32 pitch, u32 surf_w, u32 surf_h, const Glyph* g, i32 pen_x,
                 i32 pen_y, u32 color) noexcept;

// Boot self-test. Non-fatal.
void self_test() noexcept;

u64 faces_loaded() noexcept;
u64 glyphs_cached(Face* face) noexcept;
u64 cache_hits() noexcept;
u64 cache_misses() noexcept;

} // namespace notyvos::font
