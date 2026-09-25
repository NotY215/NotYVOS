# ADR 0005 — PS3 firmware isolated from NotYVFirm

Status: Accepted

## Decision
Sony PS3 firmware lives only inside the PS3 runtime domain. It must never
boot the PC, modify NotYVFirm, or become a dependency of the native kernel.