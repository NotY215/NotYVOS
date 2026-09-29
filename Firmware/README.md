# Firmware

This directory holds user-supplied firmware. Nothing here is redistributed
with NOTYVOS source.

## PS3 firmware (`PS3UPDAT.PUP`)

This file is required only when the PS3 runtime is enabled (Phase 4+).
It is a Sony-encrypted container. NOTYVOS does NOT contain:
  - Sony private or decryption keys
  - decryption bypasses
  - extraction tooling

The user is responsible for legally obtaining and preparing the firmware
component the runtime needs. NOTYVOS will not decrypt the PUP itself.

The runtime expects a decrypted module at a configured path. If the module
is missing, the runtime refuses to start with a clear message.

## NotYVFirm

NotYVFirm is NOT in this directory. It is the native PC firmware domain
and lives in `firmware/notyvfirm/` once that subsystem begins (Phase 3+).

Sony firmware and NotYVFirm are separate domains. See ADR 0005.
