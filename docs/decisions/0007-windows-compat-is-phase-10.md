# ADR 0007 — Windows compatibility is a late-phase subsystem

Status: Accepted

## Decision

Windows executable compatibility is intentionally separated from the native
kernel and PS3 runtime.

Phase 9 covers the initial Windows .exe compatibility foundation. Phase 10
covers advanced compatibility and application integration, including the
planned Brave and VLC application path.

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
