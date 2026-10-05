# NOTYVOS Fonts

This directory holds the Inter font files shipped with NOTYVOS.

## Current contents

The bundled Inter family is from the upstream Inter project.

- Upstream: https://github.com/rsms/inter
- License: SIL Open Font License 1.1
- Copyright: 2016 The Inter Project Authors
- Full license: ../THIRD_PARTY_LICENSES/inter-font.txt

NOTYVOS does not own these fonts. They are redistributed under the OFL.

## Current runtime status

Phase 11 delivered the native TrueType renderer. The kernel can parse the required Inter tables, rasterize simple glyph outlines with anti-aliased coverage, cache glyphs and draw text through the compositor.

The default runtime face is Inter Regular. Multiple runtime weights, kerning, complex-script shaping and subpixel horizontal rendering remain deferred polish.

See ../docs/fonts.md.
