# Firmware

This directory defines the firmware boundary used by NOTYVOS.

## PS3 firmware

PS3UPDAT.PUP is a Sony PS3 firmware package. NOTYVOS does not redistribute
that firmware through the repository, releases, websites, or other project
platforms.

For a developer's local build, the firmware may be placed at:

    Firmware/PS3UPDAT.PUP

The file is intentionally ignored by Git. A local ISO build may package the
developer-provided firmware into the generated ISO so the resulting
developer-owned image is self-contained for the PS3 runtime and GameRunner.

This is a build-time local packaging step, not a repository asset or a
project distribution channel. Developers are responsible for obtaining and
using the firmware in accordance with applicable rights and license terms.

## PS3 runtime connection

The packaged firmware belongs to the PS3 runtime domain only:

    Firmware/PS3UPDAT.PUP
            |
            v
      NOTYVOS ISO image
            |
            v
      PS3 runtime / GameRunner
            |
            v
       PS3 title launch

GameRunner remains responsible for the PS3 executable path. The firmware
package is not part of the native PC kernel boot dependency.

The runtime must fail clearly when a required firmware package is absent from
a developer build rather than silently using an unrelated file.

## Native firmware

NotYVFirm remains the separate native firmware project tracked by Phase 21.
It does not use Sony PS3 firmware.

Phase 21 covers:
- firmware architecture
- UEFI application loading
- optional BIOS legacy path
- firmware configuration UI
- signed firmware update and rollback

Secure Boot and TPM integration remain deferred.

## Repository policy

The repository contains no PS3UPDAT.PUP binary, Sony private keys, Sony
decryption keys, decryption bypasses, or extraction tooling.

The local firmware file is ignored by Git:

    Firmware/PS3UPDAT.PUP

See:
- docs/architecture.md
- docs/decisions/0005-ps3-firmware-isolation.md
- docs/decisions/0006-no-sony-keys-embedded.md
