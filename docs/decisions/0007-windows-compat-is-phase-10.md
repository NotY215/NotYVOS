# ADR 0007 — Windows compat is Phase 10

Status: Accepted

## Decision
Windows `.exe`/Win32 compatibility is a confirmed long-term requirement
but is a separate subsystem (PE/COFF loader, Win32/Win64 API surface,
registry, COM, SEH, etc.). It is Phase 10 and must not block Phases 0–9.