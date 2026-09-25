# NOTYVOS Fonts

This directory holds the font files shipped with NOTYVOS.

## Current contents

The Inter font family, by Rasmus Andersson.

- Upstream: https://github.com/rsms/inter
- License: SIL Open Font License 1.1 (`OFL-1.1`)
- Copyright: 2016 The Inter Project Authors
- Full license: `../THIRD_PARTY_LICENSES/inter-font.txt`

NOTYVOS does NOT own these fonts. They are redistributed unmodified under
the terms of the OFL. See `../NOTICE` and `../THIRD_PARTY_LICENSES/`.

## Expected layout

    Fonts/
        Inter-Black.ttf
        Inter-BlackItalic.ttf
        Inter-Bold.ttf
        ... (36 files total)
        InterDisplay-Thin.ttf
        InterDisplay-ThinItalic.ttf

Every TTF provided by the upstream Inter release is included. NOTYVOS
will use different weights and styles per UI element as the desktop
shell and applications are developed.

## Status

- **Present**: the font files and their license.
- **Not yet loaded**: the kernel does not read these files at boot.
- **Planned**: Phase 3 introduces the font subsystem (parser, rasterizer,
  cache). See `../docs/fonts.md`.

The kernel boot console uses an embedded 8x8 bitmap font
(`kernel/src/fb/font8x8.hpp`) so that no filesystem or rasterizer is
required before Phase 3.
