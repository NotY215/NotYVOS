# ADR 0009 — Inter font family, rendering deferred to Phase 3

Status: Accepted
Date: 2025

## Context

NOTYVOS needs a modern, legible UI font for the desktop shell and
applications. Multiple weights and styles are required for typographic
hierarchy (titles, body, captions, disabled states, code).

The Inter family (https://github.com/rsms/inter) provides 18 weights
across two optical sizes (Inter and InterDisplay), each with an italic
variant. It is licensed under the SIL Open Font License 1.1, which
permits redistribution, embedding, and modification in a non-sold-only
product such as NOTYVOS.

## Decision

Inter is the NOTYVOS UI font family. All 36 TTF files from the upstream
release are committed under `Fonts/` and redistributed under OFL-1.1.

Actual use is deferred to Phase 3, when:

- A filesystem or initramfs exists (Phase 2F) so the files can be read.
- A kernel heap exists (Phase 1E) so parsed tables and glyph caches can
  be allocated dynamically.
- A TrueType parser and rasterizer exist (Phase 3).
- A glyph cache exists (Phase 3).

Until then, the kernel boot console uses the embedded 8x8 bitmap font
in `kernel/src/fb/font8x8.hpp`. That font is not Inter, does not need
a filesystem, and does not need a heap.

## Consequences

- NOTYVOS does not own the Inter fonts. They are third-party and are
  redistributed under OFL-1.1. Attribution lives in `NOTICE` and
  `THIRD_PARTY_LICENSES/inter-font.txt`.
- Font files are tracked in the repository (not `.gitignore`d) because
  OFL-1.1 permits redistribution.
- The desktop shell will render UI text with Inter starting Phase 3.
- The boot console continues to use the 8x8 bitmap font through
  Phases 1 and 2. This is intentional, not a limitation.

## Alternatives considered

- Bundle a bitmap font only. Rejected: does not scale, does not support
  the visual density required for the desktop shell.
- Fetch fonts at runtime. Rejected: requires network at boot, adds a
  runtime dependency, and offline installation must work.
- Use a system font stack. Rejected: a self-contained OS cannot rely on
  host fonts.
- Ship every Inter weight *and* fallback families. Deferred: only Inter
  is committed at present; additional families can be added later under
  the same ADR process.
