# NOTYVOS Font Subsystem

## Current status

The Inter font family is bundled in Fonts/, but the kernel does not yet
parse or rasterize TrueType files.

The active desktop and terminal text path uses the embedded 8x8 bitmap font
at kernel/src/fb/font8x8.hpp, scaled by the compositor.

## Why Inter is not active yet

A real TTF subsystem still requires:
1. loading font files through the VFS/initramfs,
2. TrueType table parsing,
3. glyph rasterization,
4. glyph caching,
5. integration with the Graphics API and compositor.

The current compositor can therefore render without loading the bundled TTF
files.

## Bundled font set

The repository contains the bundled Inter family from upstream, including
regular and italic styles across the available weights and display variants.

Inter is redistributed under the SIL Open Font License 1.1. See NOTICE,
THIRD_PARTY_LICENSES/inter-font.txt and Fonts/README.md.

## Planned integration

TrueType loading and rasterization remain future desktop graphics work.
Their presence in the repository does not mean the font subsystem is
complete.

The embedded bitmap font remains the current UI text renderer until a real
font renderer is implemented and validated.
