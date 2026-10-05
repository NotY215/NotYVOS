# ADR 0009 — Inter font family

Status: Accepted

## Decision

Inter is the NOTYVOS UI font family and its bundled TTF files are stored in
Fonts/ under the SIL Open Font License 1.1.

The files are actively used by the delivered Phase 11 font renderer.

## Current implementation

The desktop and terminal use the delivered TrueType renderer; the embedded bitmap font remains available as legacy fallback code. Inter TTF parsing, rasterization and glyph caching
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
