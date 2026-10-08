# Firmware

This directory defines firmware inputs used by NOTYVOS development builds.

## PS3 firmware

PS3UPDAT.PUP is a Sony PS3 firmware package. NOTYVOS does not redistribute
that firmware through the repository, releases, websites, or other project
platforms.

For a developer's local PS3 runtime build, the firmware may be placed at:

    Firmware/PS3UPDAT.PUP

The file is intentionally ignored by Git. A local ISO build may package the
developer-provided firmware into the generated ISO so the developer's image
is self-contained for the PS3 runtime and GameRunner.

## Realtek RTL8188EU Wi-Fi firmware

The current Wi-Fi driver development uses:

    Firmware/rtl8188eufw.bin

This is a firmware blob required by the RTL8188EU wireless hardware path.
It is a local firmware input and is not treated as NOTYVOS source code.

The binary must not be committed to the public repository unless its
redistribution rights have been explicitly verified. Developer builds may
provide the file locally, and the build/driver path can consume it from the
Firmware directory.

The RTL8188EU firmware is separate from PS3UPDAT.PUP and has no relationship
to the PS3 runtime firmware path.

## Firmware boundaries

    rtl8188eufw.bin
          |
          v
    RTL8188EU Wi-Fi driver
          |
          v
    NOTYVOS network stack

    PS3UPDAT.PUP
          |
          v
    NOTYVOS ISO
          |
          v
    PS3 runtime / GameRunner

NotYVFirm remains a separate Phase 21 native firmware domain.

## Repository policy

The repository contains no PS3UPDAT.PUP or other proprietary firmware blobs
unless their redistribution rights have been explicitly established.

It also contains no Sony private keys, Sony decryption keys, decryption
bypasses, or firmware extraction tooling.

Local firmware files are ignored by Git.

See:
- docs/build.md
- docs/roadmap.md
- docs/architecture.md
- docs/decisions/0005-ps3-firmware-isolation.md
- docs/decisions/0006-no-sony-keys-embedded.md
