# ADR 0008 — Keep future domains separate from implemented code

Status: Accepted

## Decision

NOTYVOS does not create empty directory trees merely to represent future
subsystems. Implemented functionality stays in the existing kernel, user and
tools trees. New top-level domains are introduced when their implementation
actually begins.

This keeps the repository structure honest about what is implemented versus
planned.

## Current mapping

| Native kernel and drivers | kernel/ / Phases 1–3 |
| Graphics API, HAL, compositor, GPU abstraction and widgets | kernel/gfx/ and kernel/gpu/ / Phases 3A–3F |
| PS3 loader, PPU, SPU, DMA and JIT | kernel/ps3/ / Phases 4A–5D |
| RSX compatibility | kernel/ps3/rsx/ / Phases 6A–6F |
| GameRunner and PS3 compatibility services | kernel/src/ps3/gamerunner.cpp and related PS3 runtime code / Phases 7A–7C |
| Rendering validation | kernel/src/ps3/rsx/self_test.cpp and related validation code / Phases 8A–8B |
| Image decoders and Image Viewer | kernel/src/img/ and kernel/src/gfx/apps.cpp / Phases 9A–9C |
| Themes, Appearance and desktop UI management | kernel/src/gfx/theme.cpp, compositor.cpp, apps.cpp and related graphics code / Phases 10A–10F |
| User programs and libc | user/ / Phase 2+ |
| Host tools | tools/ |
| PS3 firmware domain | Firmware/ / runtime support |
| Native PC firmware | NotYVFirm / Phase 21, queued |
| Windows compatibility | Phases 11–21, with Phase 11 delivered and Phase 12 next |

The absence of a planned top-level directory does not mean the subsystem is
forgotten; the roadmap is the source of truth for planned work.
