# ADR 0005 — PS3 firmware isolated from NotYVFirm

Status: Accepted

## Decision

Sony PS3 firmware belongs only to the PS3 runtime domain.

A developer may provide a local `Firmware/PS3UPDAT.PUP` for an ISO build.
When present, the local build may package that developer-provided firmware
inside the generated ISO so the developer's ISO is self-contained for the
PS3 runtime and GameRunner.

The firmware binary is not committed to Git and is not distributed by
NOTYVOS through releases, websites, or other project platforms.

The firmware must never:
- boot the native PC kernel
- modify or replace NotYVFirm
- become a dependency of the native NOTYVOS boot path
- be treated as a repository asset

The developer is responsible for obtaining and using the firmware under the
applicable rights and license terms.

## Runtime boundary

The intended flow is:

    local PS3UPDAT.PUP
            |
            v
       generated ISO
            |
            v
      PS3 runtime / GameRunner

NotYVFirm remains a separate Phase 21 native firmware domain.
