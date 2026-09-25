# NOTYVOS Font Subsystem

## Status

Planned. Nothing in `kernel/` reads TrueType files at Phase 1.

## Why nothing yet

TrueType rendering requires:

1. A way to read the files (initramfs or filesystem — Phase 2F).
2. A dynamic allocator for parsed tables and glyph caches (Phase 1E).
3. A TTF parser (Phase 3).
4. A rasterizer (Phase 3).
5. A glyph cache (Phase 3).

Building any of these before Phase 1E would mean static buffers, no
dynamic allocation, and a parser with no way to load data.

## Phase plan

- **Phases 1–2**: embedded 8x8 bitmap font (`kernel/src/fb/font8x8.hpp`)
  renders the boot console. No filesystem needed. No heap needed.
- **Phase 2F**: initramfs lands; `Fonts/` is packaged into it.
- **Phase 3A**: TTF parser lands (tables: `cmap`, `head`, `hhea`,
  `hmtx`, `maxp`, `loca`, `glyf`, `OS/2`, `name`, `post`).
- **Phase 3B**: rasterizer lands (quadratic Bézier flattening, scanline
  fill, 4x4 or 8x8 supersampling for antialiasing).
- **Phase 3C**: glyph cache lands (keyed by codepoint, size, weight,
  style; LRU-evicted; backed by kernel heap).
- **Phase 3D**: desktop shell consumes the font subsystem for all UI
  text.

## Font set

All 36 TTF files from upstream Inter are committed under `Fonts/`.
The subsystem will expose weights by name:

    Inter-Thin            100
    Inter-ExtraLight      200
    Inter-Light           300
    Inter-Regular         400
    Inter-Medium          500
    Inter-SemiBold        600
    Inter-Bold            700
    Inter-ExtraBold       800
    Inter-Black           900

    Same nine weights again as "InterDisplay-*" for large sizes.

    Each weight has a corresponding Italic.

The desktop shell will choose weight and optical size by UI role. That
mapping is a Phase 3 decision, not a Phase 1 decision.
