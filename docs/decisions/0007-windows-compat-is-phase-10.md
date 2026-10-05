# ADR 0007 — Windows compatibility is a late-phase subsystem

Status: Accepted

## Decision

Windows executable compatibility is intentionally separated from the native
kernel and PS3 runtime.

Windows executable compatibility is intentionally kept after the completed Phase 1–10 native/runtime and desktop roadmap. Later compatibility work is tracked as Phase 11 and Phase 12, including Windows application integration such as the planned Brave and VLC path. The delivered 10C–10F work remains native desktop UI and does not move Windows compatibility earlier.

The compatibility subsystem is not allowed to become an implicit dependency
of the native Phases 1 through 8.

## Scope

Future Windows compatibility may require:
- PE/COFF loading
- Win32/Win64 API compatibility
- process and DLL semantics
- registry and configuration services
- COM
- SEH and related runtime behavior

These items are future work and are not currently implemented.
