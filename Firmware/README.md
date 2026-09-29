# Firmware

This directory contains the user-supplied PS3 firmware domain. Firmware
domains remain separate from the native PC kernel.

## PS3 firmware

PS3UPDAT.PUP, when present, is user-supplied and is required only by later
PS3 runtime stages that need Sony firmware components.

NOTYVOS does not redistribute Sony firmware and does not contain:
- Sony private keys
- Sony decryption keys
- decryption bypasses
- extraction tooling

The user is responsible for legally obtaining and preparing any firmware
component required by the runtime.

## Firmware domains

### Native PC domain

NotYVFirm is the planned native PC firmware domain. It is separate from the
PS3 runtime and does not use Sony PS3 firmware.

### PS3 runtime domain

Sony PS3 firmware belongs only to the PS3 runtime. It must not become a
dependency of the native NOTYVOS boot path.

See docs/architecture.md, docs/decisions/0005-ps3-firmware-isolation.md and
docs/decisions/0006-no-sony-keys-embedded.md.
