# ADR 0009 — Inter font family

Status: Accepted

## Decision

Inter is the NOTYVOS UI font family and its bundled TTF files are stored in
Fonts/ under the SIL Open Font License 1.1.

The files are available to the project, but their presence does not imply
that the kernel font renderer is implemented.

## Current implementation

The desktop and terminal currently use the embedded 8x8 bitmap font from
kernel/src/fb/font8x8.hpp. Inter TTF parsing, rasterization and glyph caching
are not currently implemented.

## Future integration

A future graphics/font phase will:
- load Inter through the VFS or packaged resources
- parse required TrueType tables
- rasterize glyphs
- cache glyphs
- connect the renderer to the Graphics API and compositor

This ADR governs the bundled font choice without marking the renderer as
complete.
