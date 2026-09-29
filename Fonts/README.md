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

The font files are present in the repository but are not loaded by the
kernel. Desktop and terminal text currently use the embedded 8x8 bitmap font
in kernel/src/fb/font8x8.hpp.

## Future use

A future font subsystem will load Inter through the VFS, parse TrueType
tables, rasterize glyphs and provide cached glyphs to the Graphics API.

See ../docs/fonts.md.
