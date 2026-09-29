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

| Implemented or planned area | Current location / phase |
|---|---|
| Native kernel and drivers | kernel/ / Phases 1–3 |
| Graphics API, HAL, compositor and widgets | kernel/gfx/ / Phases 3A–3E |
| PS3 loader, PPU, SPU, DMA and JIT | kernel/ps3/ / Phases 4A–5D |
| User programs and libc | user/ / Phase 2+ |
| Host tools | tools/ |
| PS3 firmware domain | Firmware/ / later runtime stages |
| Native PC firmware | NotYVFirm, planned |
| RSX compatibility | Phase 6, planned |
| GameRunner and compatibility layer | Phase 7, planned |
| Rendering validation | Phase 8, planned |
| Windows compatibility | Phases 9–10, planned |

The absence of a planned top-level directory does not mean the subsystem is
forgotten; the roadmap is the source of truth for planned work.
