# ADR 0009 — Inter font family

Status: Accepted

## Decision

Inter is the NOTYVOS UI font family and its bundled TTF files are stored in
Fonts/ under the SIL Open Font License 1.1.

The Phase 11 TrueType subsystem now actively uses the bundled Inter family.

## Current implementation

The delivered renderer parses the required TrueType tables, extracts simple
glyph outlines, flattens quadratic Bézier curves, produces anti-aliased
coverage through 4× vertical supersampling, caches glyphs and integrates
font::draw_text() with the compositor.

The default runtime face is Inter Regular.

## Deferred polish

Multiple runtime weights, kerning, complex-script shaping and subpixel
horizontal rendering remain deferred.

This ADR governs the bundled font choice and the completed Phase 11 renderer.
