# ADR 0006 — No Sony keys, no decryption bypass

Status: Accepted

## Decision

NOTYVOS contains no Sony private keys, no decryption keys, no bypass
mechanisms, and no extraction tooling.

Developers may provide their own legally obtained PS3 firmware package at
`Firmware/PS3UPDAT.PUP` for a local ISO build. The file is ignored by Git
and is not shared by the project.

The local ISO build may embed that developer-provided firmware so the
developer's ISO contains the firmware required by the PS3 runtime and
GameRunner. This does not make the firmware part of the repository or a
NOTYVOS release.

If a required firmware package is missing, the build/runtime must report the
missing dependency clearly.

No mechanism is provided to decrypt, extract, or bypass protections on Sony
firmware.
